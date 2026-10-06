#include "review_export.h"
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <fstream>

namespace
{
using namespace flashcards;
using Json = nlohmann::json;
struct ExportFixture
{
    std::filesystem::path root = std::filesystem::temp_directory_path() / ("draxul-review-export-" + new_review_id());
    std::optional<std::string> recall, outbox;
    int writes = 0, box_writes = 0, fail_box_at = 0;
    bool fail_before = false, fail_after = false;
    int64_t now = 1'800'000'000;
    unsigned sequence = 0;
    ~ExportFixture() { std::error_code error; std::filesystem::remove_all(root, error); }
    ReviewExport exporter(std::filesystem::path shared = {})
    {
        return ReviewExport(shared.empty() ? root / "shared" : shared, root / "cache",
            [&] { return outbox; }, [&](std::string_view value) {
                if (++box_writes == fail_box_at) throw std::runtime_error("Outbox disk failure");
                outbox = value;
            }, [&] { return recall; }, [&](std::string_view value) {
                if (fail_before) throw std::runtime_error("Recall disk failure before replacement");
                recall = value; ++writes;
                if (fail_after) throw std::runtime_error("Recall disk failure after replacement");
            }, [&] { return now; }, [&] {
                char suffix[13]{};
                std::snprintf(suffix, sizeof(suffix), "%012x", ++sequence);
                return std::string("12345678-1234-4123-8123-") + suffix;
            });
    }
    ReviewSession session(ReviewExport& exports)
    {
        return ReviewSession({{"fixture-word", "kana", "fixture", "image.png", "fixture"}},
            [&] { return recall; }, [&](std::string_view value) { exports.commit(value); }, [&] { return now; });
    }
    size_t events(const std::filesystem::path& shared = {})
    {
        size_t count = 0;
        const auto directory = (shared.empty() ? root / "shared" : shared) / "flashcard-reviews";
        if (std::filesystem::exists(directory))
            for (const auto& file : std::filesystem::recursive_directory_iterator(directory))
                if (file.is_regular_file() && file.path().extension() == ".json" && file.path().filename() != "summary-v1.json"
                    && !file.path().filename().string().starts_with("checkpoint-")) ++count;
        return count;
    }
};
bool grade(ReviewSession& session, bool remembered)
{
    session.set_image_ready(true); session.flip(); return session.grade(remembered);
}
Json file_json(const std::filesystem::path& file) { std::ifstream stream(file); return Json::parse(stream); }
bool settle(ReviewExport& exports)
{
    for (int pass = 0; pass < 16; ++pass)
    {
        if (exports.flush()) return true;
        if (!exports.catching_up()) break;
    }
    return false;
}
}

TEST_CASE("Explicit grades keep durable export intent through offline reopen and idempotent delivery", "[flashcards][exports]")
{
    ExportFixture fixture;
    auto exports = fixture.exporter();
    auto session = fixture.session(exports);
    REQUIRE(session.flip());
    REQUIRE_FALSE(session.grade(true));
    REQUIRE_FALSE(fixture.outbox);
    session.set_image_ready(true);
    REQUIRE(session.grade(false));
    const auto history = fixture.recall;
    REQUIRE(exports.pending() == 1);
    REQUIRE_FALSE(exports.flush()); // Shared root is absent; never create a fake Dropbox root.
    REQUIRE(fixture.recall == history);
    REQUIRE(fixture.events() == 0);
    auto reopened = fixture.exporter();
    REQUIRE_FALSE(reopened.flush());
    REQUIRE(reopened.pending() == 1);
    REQUIRE_FALSE(fixture.session(reopened).current());
    std::filesystem::create_directories(fixture.root / "shared");
    REQUIRE(reopened.flush());
    REQUIRE(reopened.pending() == 0);
    REQUIRE(fixture.events() == 1);
    REQUIRE(reopened.flush());
    REQUIRE(fixture.events() == 1);
    REQUIRE(fixture.writes == 1);
    REQUIRE(fixture.recall == history);
    const auto box = Json::parse(*fixture.outbox);
    const auto event = box["latest"]["recognition:fixture-word"];
    REQUIRE(event["outcome"] == "Again");
    REQUIRE(event["reviewed_at_unix"] == fixture.now);
    REQUIRE(event["explicit"] == true);
    REQUIRE(box["delivered_count"] == 1);
    REQUIRE(file_json(fixture.root / "shared/flashcard-reviews" / box["producer_id"].get<std::string>() / "summary-v1.json")["event_count"] == 1);
}

TEST_CASE("Immediate additional rounds export only explicit grades and converge on a fresh device", "[flashcards][exports]")
{
    ExportFixture first, fresh;
    fresh.sequence = 100;
    std::filesystem::create_directories(first.root / "shared");
    auto exports = first.exporter();
    auto session = first.session(exports);
    REQUIRE(grade(session, false));
    REQUIRE(exports.flush());
    REQUIRE_FALSE(session.current());
    const auto before = first.recall;
    ++first.now; // Still inside the ten-minute cooldown.
    session.start_batch(true);
    REQUIRE(first.recall == before);
    REQUIRE(first.events() == 1);
    session.skip();
    REQUIRE(first.events() == 1);
    session.start_batch(true);
    REQUIRE(grade(session, true));
    REQUIRE_FALSE(session.current());
    REQUIRE(exports.flush());
    const auto repeated = first.recall;
    REQUIRE(parse_state(*repeated).at("recognition:fixture-word").reviews == 2);
    REQUIRE(parse_state(*repeated).at("recognition:fixture-word").remembered == 1);
    REQUIRE(first.events() == 2);
    fresh.now = first.now;
    auto imported = fresh.exporter(first.root / "shared");
    REQUIRE(imported.flush());
    REQUIRE(parse_state(*fresh.recall) == parse_state(*repeated));
    REQUIRE_FALSE(fresh.session(imported).current());
    REQUIRE(first.events() == 2);
}

TEST_CASE("A failed recall save aborts prepared export while an after-replacement error never double grades", "[flashcards][exports]")
{
    ExportFixture fixture;
    std::filesystem::create_directories(fixture.root / "shared");
    auto exports = fixture.exporter();
    auto session = fixture.session(exports);
    fixture.fail_before = true;
    REQUIRE_FALSE(grade(session, true));
    REQUIRE_FALSE(fixture.recall);
    REQUIRE(exports.flush());
    REQUIRE(fixture.events() == 0);
    REQUIRE(exports.pending() == 0);
    fixture.fail_before = false;
    fixture.fail_after = true;
    REQUIRE(session.grade(true)); // Verified saved grade despite returned storage error.
    REQUIRE(fixture.writes == 1);
    const auto history = fixture.recall;
    REQUIRE(exports.flush());
    REQUIRE(fixture.events() == 1);
    REQUIRE_FALSE(session.grade(true));
    REQUIRE_FALSE(fixture.session(exports).current());
    REQUIRE(fixture.recall == history);
}

TEST_CASE("Export intent failure blocks a grade and delivery acknowledgement failure safely repeats the same ID", "[flashcards][exports]")
{
    ExportFixture fixture;
    std::filesystem::create_directories(fixture.root / "shared");
    auto exports = fixture.exporter();
    auto session = fixture.session(exports);
    fixture.fail_box_at = 1;
    REQUIRE_FALSE(grade(session, true));
    REQUIRE_FALSE(fixture.recall);
    REQUIRE_FALSE(fixture.outbox);
    fixture.fail_box_at = 0;
    REQUIRE(session.grade(true));
    // Resolve prepared -> pending succeeds, but acknowledging published event fails.
    fixture.fail_box_at = fixture.box_writes + 2;
    REQUIRE_FALSE(exports.flush());
    REQUIRE(fixture.events() == 1);
    REQUIRE(exports.pending() == 1);
    fixture.fail_box_at = 0;
    REQUIRE(exports.flush());
    REQUIRE(fixture.events() == 1);
    REQUIRE(Json::parse(*fixture.outbox)["delivered_count"] == 1);
    REQUIRE(fixture.writes == 1);
}

TEST_CASE("Offline producers and a fresh device converge with shared cooldown and due-only selection", "[flashcards][exports]")
{
    ExportFixture first, second;
    second.sequence = 100;
    const auto shared = first.root / "shared";
    auto a = first.exporter(shared), b = second.exporter(shared);
    auto one = first.session(a), two = second.session(b);
    REQUIRE(grade(one, false));
    REQUIRE(grade(two, true));
    REQUIRE_FALSE(a.flush()); REQUIRE_FALSE(b.flush());
    std::filesystem::create_directories(shared);
    REQUIRE(a.flush()); REQUIRE(b.flush());
    REQUIRE(first.events(shared) == 2);
    REQUIRE(Json::parse(*first.outbox)["producer_id"] != Json::parse(*second.outbox)["producer_id"]);
    REQUIRE(a.flush()); REQUIRE(b.flush());
    REQUIRE(first.events(shared) == 2);
    REQUIRE(parse_state(*first.recall) == parse_state(*second.recall));
    const auto merged = parse_state(*first.recall).at("recognition:fixture-word");
    REQUIRE(merged.reviews == 2); REQUIRE(merged.forgotten == 1); REQUIRE(merged.remembered == 1);
    REQUIRE(merged.stage == 0); REQUIRE(merged.due == first.now + 600);
    ExportFixture fresh; fresh.sequence = 200;
    auto c = fresh.exporter(shared);
    REQUIRE(c.flush());
    REQUIRE(parse_state(*fresh.recall) == parse_state(*first.recall));
    auto next = fresh.session(c);
    REQUIRE_FALSE(next.current());
    fresh.now += 600;
    next.refresh();
    REQUIRE(next.current());
    REQUIRE(grade(next, true));
    first.now = second.now = fresh.now;
    REQUIRE(c.flush()); REQUIRE(a.flush()); REQUIRE(b.flush());
    REQUIRE(parse_state(*first.recall) == parse_state(*fresh.recall));
    REQUIRE(parse_state(*first.recall) == parse_state(*second.recall));
    REQUIRE(parse_state(*first.recall).at("recognition:fixture-word").reviews == 3);
}

TEST_CASE("Existing aggregate scores bootstrap a fresh device without fabricated historical events", "[flashcards][exports]")
{
    ExportFixture first, fresh;
    fresh.sequence = 100;
    Progress prior;
    prior.stage = 3; prior.reviews = 10; prior.remembered = 8; prior.forgotten = 1; prior.assisted = 1;
    prior.last_reviewed = first.now - 86400; prior.due = first.now + 86400;
    State existing{{"recognition:fixture-word", prior}};
    for (int index = 0; index < 100; ++index) existing["production:retained-" + std::to_string(index)] = prior;
    first.recall = serialize_state(existing);
    const auto original = first.recall;
    std::filesystem::create_directories(first.root / "shared");
    auto a = first.exporter(), b = fresh.exporter(first.root / "shared");
    REQUIRE(settle(a)); REQUIRE(settle(b));
    REQUIRE(first.recall == original);
    REQUIRE(parse_state(*fresh.recall) == parse_state(*original));
    REQUIRE(first.events() == 0);
    INFO(a.error());
    REQUIRE(settle(a)); // Aggregate checkpoints exceed the small event-file bound.
    fresh.now += 86400;
    auto session = fresh.session(b);
    REQUIRE(grade(session, true)); REQUIRE(settle(b));
    first.now = fresh.now;
    REQUIRE(settle(a));
    const auto updated = parse_state(*first.recall).at("recognition:fixture-word");
    REQUIRE(updated.reviews == 11); REQUIRE(updated.assisted == 1); REQUIRE(updated.remembered == 9);
    REQUIRE(first.events() == 1);
}

TEST_CASE("An imported review preserves a revealed answer until its stale grade is rejected", "[flashcards][exports]")
{
    ExportFixture first, peer;
    peer.sequence = 100;
    std::filesystem::create_directories(first.root / "shared");
    auto a = first.exporter(), b = peer.exporter(first.root / "shared");
    REQUIRE(a.flush()); REQUIRE(b.flush());
    auto visible = first.session(a), other = peer.session(b);
    visible.set_image_ready(true); REQUIRE(visible.flip());
    REQUIRE(grade(other, false)); REQUIRE(b.flush()); REQUIRE(a.flush());
    visible.refresh();
    REQUIRE(visible.current()); REQUIRE(visible.revealed());
    const auto imported = first.recall;
    REQUIRE_FALSE(visible.grade(true));
    REQUIRE(first.recall == imported); REQUIRE_FALSE(visible.current());
    visible.refresh(true);
    REQUIRE_FALSE(visible.error().empty()); // Passive sync must not erase the rejection explanation.
    REQUIRE(first.events() == 1);
}

TEST_CASE("Out-of-order orphan and partial files keep cached scores until their bootstrap arrives", "[flashcards][exports]")
{
    ExportFixture peer, fresh;
    fresh.sequence = 100;
    auto b = peer.exporter(); auto session = peer.session(b);
    REQUIRE(grade(session, false)); REQUIRE_FALSE(b.flush());
    const auto box = Json::parse(*peer.outbox), event = box["pending"][0];
    const auto shared = fresh.root / "shared";
    const auto folder = shared / "flashcard-reviews" / box["producer_id"].get<std::string>();
    std::filesystem::create_directories(folder);
    { std::ofstream stream(folder / (event["event_id"].get<std::string>() + ".json")); stream << event; }
    auto a = fresh.exporter();
    REQUIRE_FALSE(a.flush()); REQUIRE_FALSE(fresh.recall);
    const auto checkpoint_path = folder / ("checkpoint-" + box["checkpoint"]["checkpoint_id"].get<std::string>() + ".json");
    { std::ofstream stream(checkpoint_path); stream << "partial"; }
    REQUIRE_FALSE(a.flush()); REQUIRE_FALSE(fresh.recall);
    { std::ofstream stream(checkpoint_path); stream << box["checkpoint"]; }
    REQUIRE(a.flush());
    REQUIRE(parse_state(*fresh.recall) == parse_state(*peer.recall));
    auto restarted = fresh.exporter();
    REQUIRE(restarted.flush()); REQUIRE(fresh.writes == 1);
    const auto actual = fresh.recall;
    { std::ofstream stream(folder / "conflicted copy.json"); stream << event; }
    REQUIRE_FALSE(restarted.flush()); REQUIRE(fresh.recall == actual);
}

TEST_CASE("Replay deduplicates reordered raw history and retains alternative aggregate baselines", "[flashcards][exports]")
{
    ExportFixture first, second;
    second.sequence = 100;
    auto a = first.exporter(), b = second.exporter();
    auto one = first.session(a), two = second.session(b);
    REQUIRE(grade(one, false)); REQUIRE(grade(two, true));
    REQUIRE_FALSE(a.flush()); REQUIRE_FALSE(b.flush());
    const auto x = Json::parse(*first.outbox), y = Json::parse(*second.outbox);
    const auto xc = x["checkpoint"].dump(), yc = y["checkpoint"].dump();
    const auto xe = x["pending"][0].dump(), ye = y["pending"][0].dump();
    const auto forward = rebuild_review_history({xc, yc}, {xe, ye}, first.now);
    const auto reverse = rebuild_review_history({yc, xc, yc}, {ye, xe, ye}, first.now);
    REQUIRE(forward.state == reverse.state); REQUIRE(reverse.events == 2); REQUIRE(reverse.checkpoints == 2);
    auto baseline = x["checkpoint"], alternative = y["checkpoint"];
    Progress prior; prior.reviews = 8; prior.remembered = 8; prior.stage = 4;
    prior.last_reviewed = first.now; prior.due = first.now + 86400;
    baseline["recall"] = Json::parse(serialize_state({{"recognition:fixture-word", prior}}));
    prior.reviews = 7; prior.remembered = 7;
    alternative["recall"] = Json::parse(serialize_state({{"recognition:fixture-word", prior}}));
    const auto bootstrapped = rebuild_review_history({baseline.dump(), alternative.dump()}, {xe, ye}, first.now);
    REQUIRE(bootstrapped.alternative_bootstraps);
    REQUIRE(bootstrapped.state.at("recognition:fixture-word").reviews == 8);
    REQUIRE(bootstrapped.events == 2); // Covered events remain archived, never counted twice.
}

TEST_CASE("Detected sync gaps and bounded catch-up preserve scheduling until raw history is complete", "[flashcards][exports]")
{
    ExportFixture peer, fresh;
    fresh.sequence = 100;
    std::filesystem::create_directories(peer.root / "shared");
    auto b = peer.exporter(); auto session = peer.session(b);
    REQUIRE(grade(session, false)); REQUIRE(b.flush());
    const auto box = Json::parse(*peer.outbox), original = box["latest"]["recognition:fixture-word"];
    const auto folder = peer.root / "shared/flashcard-reviews" / box["producer_id"].get<std::string>();
    const auto path = folder / (original["event_id"].get<std::string>() + ".json");
    std::filesystem::remove(path); // Test-only simulated Dropbox delivery gap.
    auto a = fresh.exporter(peer.root / "shared");
    REQUIRE_FALSE(a.flush()); REQUIRE_FALSE(fresh.recall);
    { std::ofstream stream(path); stream << original; }
    REQUIRE(settle(a));
    REQUIRE(parse_state(*fresh.recall) == parse_state(*peer.recall));
    // Test-only raw fixtures make a backlog larger than one poll's 64-file cap.
    for (int index = 2; index <= 70; ++index)
    {
        auto event = original;
        char suffix[13]{}; std::snprintf(suffix, sizeof(suffix), "%012x", index + 1000);
        event["event_id"] = std::string("12345678-1234-4123-8123-") + suffix;
        event["producer_sequence"] = index; event["local_review_number"] = index;
        std::ofstream stream(folder / (event["event_id"].get<std::string>() + ".json")); stream << event;
    }
    const auto prior = fresh.recall;
    REQUIRE_FALSE(a.flush()); REQUIRE(a.catching_up()); REQUIRE(fresh.recall == prior);
    REQUIRE(settle(a));
    REQUIRE(parse_state(*fresh.recall).at("recognition:fixture-word").reviews == 70);
    const auto complete = fresh.recall;
    auto restarted = fresh.exporter(peer.root / "shared");
    REQUIRE(settle(restarted)); REQUIRE(fresh.recall == complete);
}

TEST_CASE("An inconsistent prepared intent preserves local history and pauses export and grading", "[flashcards][exports]")
{
    ExportFixture fixture;
    std::filesystem::create_directories(fixture.root / "shared");
    auto exports = fixture.exporter(); auto session = fixture.session(exports);
    REQUIRE(grade(session, false));
    auto altered = parse_state(*fixture.recall);
    ++altered.at("recognition:fixture-word").reviews;
    ++altered.at("recognition:fixture-word").remembered;
    fixture.recall = serialize_state(altered);
    const auto history = fixture.recall, intent = fixture.outbox;
    REQUIRE_FALSE(exports.flush()); REQUIRE(fixture.events() == 0);
    fixture.now += 600;
    auto reopened = fixture.session(exports);
    REQUIRE_FALSE(grade(reopened, true));
    REQUIRE(fixture.recall == history); REQUIRE(fixture.outbox == intent);
}

TEST_CASE("Conflicted event IDs and inconsistent prepared history are preserved and diagnosed", "[flashcards][exports]")
{
    ExportFixture fixture;
    std::filesystem::create_directories(fixture.root / "shared");
    auto exports = fixture.exporter();
    auto session = fixture.session(exports);
    REQUIRE(grade(session, false));
    auto box = Json::parse(*fixture.outbox);
    const auto event = box["tentative"]["event"];
    const auto directory = fixture.root / "shared/flashcard-reviews" / event["producer_id"].get<std::string>();
    std::filesystem::create_directories(directory);
    const auto path = directory / (event["event_id"].get<std::string>() + ".json");
    { std::ofstream stream(path); stream << "partial"; }
    REQUIRE_FALSE(exports.flush());
    REQUIRE(exports.pending() == 1);
    REQUIRE(fixture.events() == 1);
    { std::ifstream stream(path); std::string text; stream >> text; REQUIRE(text == "partial"); }
    // Corrupt local outbox cannot silently disable exporting and allow new grades.
    fixture.outbox = "{\"schema_version\":2}";
    fixture.now += 600;
    auto later = fixture.session(exports);
    const auto history = fixture.recall;
    REQUIRE_FALSE(grade(later, true));
    REQUIRE(fixture.recall == history);
    REQUIRE(fixture.outbox == "{\"schema_version\":2}");
}
