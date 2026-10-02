#include "review.h"
#include "review_lock.h"
#include "flip.h"
#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <stdexcept>
#include <chrono>
#include <filesystem>

namespace
{
using namespace flashcards;
struct DurableStore
{
    std::optional<std::string> saved;
    bool fail = false;
    int writes = 0;
    int64_t now = 1'800'000'000;
    std::vector<Card> cards{ { "apple", "kana", "ringo", "apple.png", "license" } };
    ReviewSession open()
    {
        return ReviewSession(cards, [&] { return saved; }, [&](std::string_view text) {
            if (fail) throw std::runtime_error("disk full");
            saved = text;
            ++writes;
        }, [&] { return now; });
    }
    Progress progress() { return parse_state(*saved).at("recognition:apple"); }
};
bool recalled(ReviewSession& s, bool correct)
{
    s.set_image_ready(true);
    s.flip();
    return s.grade(correct);
}
}

TEST_CASE("Recognition persists explicit grades across reopened and rebuilt decks", "[flashcards]")
{
    DurableStore storage;
    auto review = storage.open();
    REQUIRE(review.current());
    REQUIRE_FALSE(review.grade(true));
    REQUIRE(review.flip());
    REQUIRE_FALSE(review.grade(true)); // No vetted meaning association rendered.
    review.set_image_ready(true);
    REQUIRE(review.grade(true));
    REQUIRE(storage.progress().stage == 1);
    REQUIRE(storage.progress().due == storage.now + 86400);
    REQUIRE_FALSE(review.grade(true));
    REQUIRE(storage.writes == 1);
    REQUIRE_FALSE(storage.open().current());
    storage.now += 86400 - 1;
    REQUIRE_FALSE(storage.open().current());
    ++storage.now;
    auto reopened = storage.open();
    REQUIRE(reopened.current());
    REQUIRE(recalled(reopened, true));
    REQUIRE(storage.progress().due == storage.now + 3 * 86400);

    storage.cards = { { "new", "new kana", "", "new.png", "" } };
    auto changed_deck = storage.open();
    REQUIRE(changed_deck.current()->id == "new");
    REQUIRE(recalled(changed_deck, false));
    REQUIRE(parse_state(*storage.saved).size() == 2); // Retain temporarily removed ID.
    storage.cards = { { "apple", "edited kana", "", "apple.png", "" } };
    REQUIRE_FALSE(storage.open().current());
}

TEST_CASE("Remembered expands bounded intervals while Again relearns", "[flashcards]")
{
    DurableStore storage;
    constexpr int intervals[] = { 1, 3, 7, 14, 30, 30, 30 };
    for (int interval : intervals)
    {
        auto review = storage.open();
        REQUIRE(recalled(review, true));
        REQUIRE(storage.progress().due == storage.now + interval * 86400);
        storage.now = storage.progress().due;
    }
    auto forgot = storage.open();
    REQUIRE(recalled(forgot, false));
    REQUIRE(storage.progress().stage == 0);
    REQUIRE(storage.progress().due == storage.now + 600);
    REQUIRE(storage.progress().forgotten == 1);
}

TEST_CASE("Embedded words create independent directions and retain existing recognition progress", "[flashcards]")
{
    DurableStore storage;
    auto first = storage.open();
    REQUIRE(recalled(first, true));
    const auto original = storage.progress();
    storage.cards = parse_deck(R"({"schema_version":2,"kind":"bidirectional","cards":[
        {"id":"apple","kana":"りんご","romaji":"ringo","image":"apple.jpg",
         "attribution":"license","audio":"ringo.wav","audio_attribution":"voice"}]})");
    REQUIRE(storage.cards.size() == 2);
    auto production = storage.open();
    REQUIRE(production.current()->direction == Direction::Production);
    REQUIRE_FALSE(production.grade(true));
    REQUIRE(recalled(production, false));
    auto state = parse_state(*storage.saved);
    REQUIRE(state.at("recognition:apple") == original);
    REQUIRE(state.at("production:apple").forgotten == 1);
    REQUIRE(state.at("production:apple").due == storage.now + 600);
    REQUIRE_FALSE(storage.open().current());
    storage.now += 600;
    REQUIRE(storage.open().current()->direction == Direction::Production);
    state.at("recognition:apple").assisted = 2;
    state.at("recognition:apple").reviews += 2;
    storage.saved = serialize_state(state);
    auto repeated = storage.open();
    REQUIRE(recalled(repeated, true));
    REQUIRE(parse_state(*storage.saved).at("recognition:apple").assisted == 2);
}

TEST_CASE("Skipping a missing picture keeps each direction independently due", "[flashcards]")
{
    DurableStore storage;
    storage.cards.push_back(storage.cards.front());
    storage.cards.back().direction = Direction::Production;
    auto review = storage.open();
    review.skip();
    REQUIRE(review.current()->direction == Direction::Production);
    REQUIRE_FALSE(review.grade(true));
    review.skip();
    REQUIRE_FALSE(review.current());
    REQUIRE(review.due_count() == 2);
    REQUIRE(storage.writes == 0);
}

TEST_CASE("Card turn changes faces at the edge and completes within its duration", "[flashcards]")
{
    auto front = sample_flip(0);
    REQUIRE(front.width == 1);
    REQUIRE_FALSE(front.back);
    auto middle = sample_flip(0.29);
    REQUIRE(middle.width < 0.02f);
    REQUIRE_FALSE(middle.finished);
    auto back = sample_flip(0.58);
    REQUIRE(back.width == 1);
    REQUIRE(back.back);
    REQUIRE(back.finished);
    REQUIRE_FALSE(sample_flip(-1).finished);
    REQUIRE(sample_flip(100).finished);
}

TEST_CASE("Failed saves preserve the revealed card and can be retried", "[flashcards]")
{
    DurableStore storage;
    auto review = storage.open();
    storage.fail = true;
    REQUIRE_FALSE(recalled(review, true));
    REQUIRE_FALSE(storage.saved.has_value());
    REQUIRE(review.revealed());
    REQUIRE(review.current());
    REQUIRE_FALSE(review.error().empty());
    storage.fail = false;
    REQUIRE(review.grade(true));
    REQUIRE(storage.progress().reviews == 1);
}

TEST_CASE("Two panes cannot grade a stale card or overwrite unrelated history", "[flashcards]")
{
    DurableStore storage;
    auto first = storage.open();
    auto stale = storage.open();
    REQUIRE(recalled(first, true));
    REQUIRE_FALSE(recalled(stale, false));
    REQUIRE(storage.progress().remembered == 1);
    REQUIRE(storage.progress().forgotten == 0);
    REQUIRE(storage.writes == 1);
}

TEST_CASE("Corrupt and unsupported review state is preserved and blocks grading", "[flashcards]")
{
    DurableStore storage;
    for (const auto* bad : { "bad json", "{\"schema_version\":2,\"cards\":{}}", "{\"schema_version\":1,\"schema_version\":1,\"cards\":{}}" })
    {
        storage.saved = bad;
        auto review = storage.open();
        REQUIRE(review.blocked());
        REQUIRE_FALSE(review.current());
        REQUIRE_FALSE(review.flip());
        REQUIRE_FALSE(review.grade(false));
        REQUIRE(*storage.saved == bad);
    }
    REQUIRE(storage.writes == 0);
    storage.saved.reset();
    auto empty = storage.open();
    REQUIRE_FALSE(empty.blocked());
    storage.cards.clear();
    auto no_deck = storage.open();
    REQUIRE(no_deck.deck_size() == 0);
    REQUIRE_FALSE(no_deck.current());
    REQUIRE_FALSE(no_deck.next_due());
}

TEST_CASE("Missing images can be skipped without inventing grades", "[flashcards]")
{
    DurableStore storage;
    auto review = storage.open();
    review.flip();
    REQUIRE_FALSE(review.grade(true));
    review.skip();
    REQUIRE_FALSE(review.current());
    REQUIRE(review.due_count() == 1);
    REQUIRE(storage.writes == 0);
}

TEST_CASE("Review file lock excludes overlapping writers and releases on failure", "[flashcards]")
{
    const auto directory = std::filesystem::temp_directory_path() / ("draxul-flashcards-lock-"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup
    {
        std::filesystem::path path;
        ~Cleanup() { std::error_code error; std::filesystem::remove_all(path, error); }
    } cleanup{ directory };
    auto first = flashcards::lock_review(directory);
    REQUIRE_THROWS(flashcards::lock_review(directory));
    first.reset();
    REQUIRE_NOTHROW(flashcards::lock_review(directory));
    REQUIRE_THROWS(flashcards::lock_review({}));
}

TEST_CASE("Large due decks stop after twenty explicit grades and allow another batch", "[flashcards]")
{
    DurableStore storage;
    storage.cards.clear();
    for (int i = 0; i < 21; ++i)
        storage.cards.push_back({ "card-" + std::to_string(i), "kana", "", "image.png", "" });
    auto review = storage.open();
    for (int i = 0; i < 20; ++i)
        REQUIRE(recalled(review, true));
    REQUIRE(review.batch_finished());
    REQUIRE_FALSE(review.current());
    REQUIRE(review.due_count() == 1);
    review.refresh();
    REQUIRE_FALSE(review.current());
    review.start_batch();
    REQUIRE(review.current());
    REQUIRE(recalled(review, false));
    REQUIRE(review.due_count() == 0);
    REQUIRE(storage.writes == 21);
}
