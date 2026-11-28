#pragma once

// Undo and redo on SONG, CHAIN and TABLE (SELECT+UP / SELECT+DOWN, as PHRASE's PhraseEditHistory).
// Each edit keeps only the parts it changed: the song grid (every track's chain column), the chains, the
// phrases (a clone makes new ones) or the one table edited, as they were before and after. Undo restores
// them only if they are still as the edit left them; anything else changed them meanwhile, so the history
// is dropped rather than overwrite that work. Session only, never saved.

#include <string>
#include <vector>

#include "songcore/model.h"
#include "songcore/project_io.h"

namespace pt::ui {

class SequenceHistory {
public:
    enum class Result { DONE, EMPTY, DIVERGED };
    static constexpr size_t DEPTH = 24;

    // What an edit may change. `tableId` < 0: SONG / CHAIN (grid, chains, phrases); else that table.
    struct Parts {
        int tableId = -1;
        std::vector<std::vector<int>> grid;
        std::vector<songcore::Chain> chains;
        std::vector<songcore::Phrase> phrases;
        songcore::Table table;
    };

    static Parts take(const songcore::Project& p, int tableId) {
        Parts parts;
        parts.tableId = tableId;
        if (tableId >= 0) {
            if (tableId < static_cast<int>(p.tables.size())) parts.table = p.tables[static_cast<size_t>(tableId)];
            return parts;
        }
        for (const auto& t : p.tracks) parts.grid.push_back(t.chainRefs);
        parts.chains = p.chains;
        parts.phrases = p.phrases;
        return parts;
    }

    // After an edit: keeps what changed between `before` and the project now.
    // shortcut: a SONG or CHAIN edit copies and compares all chains and phrases (as text), fine on PCs
    // and phones; measure on the slowest handheld, and compare only touched entries if it shows.
    void record(const Parts& before, const songcore::Project& now) {
        const Parts after = take(now, before.tableId);
        Entry e;
        e.tableId = before.tableId;
        if (before.tableId >= 0) {
            e.table = text(before.table) != text(after.table);
        } else {
            e.grid = before.grid != after.grid;
            e.chains = pool_text(before.chains, chain_emit) != pool_text(after.chains, chain_emit);
            e.phrases = pool_text(before.phrases, phrase_emit) != pool_text(after.phrases, phrase_emit);
        }
        if (!e.grid && !e.chains && !e.phrases && !e.table) return;
        e.before = before;
        e.after = after;
        undo_.push_back(std::move(e));
        if (undo_.size() > DEPTH) undo_.erase(undo_.begin());
        redo_.clear();
    }

    Result apply(songcore::Project& p, bool redo) {
        auto& from = redo ? redo_ : undo_;
        auto& to = redo ? undo_ : redo_;
        if (from.empty()) return Result::EMPTY;
        Entry e = from.back();
        // The project must still be as this step left it (`after` for undo, `before` for redo).
        if (!matches(p, e, redo ? e.before : e.after)) {
            clear();
            return Result::DIVERGED;
        }
        restore(p, e, redo ? e.after : e.before);
        from.pop_back();
        to.push_back(std::move(e));
        return Result::DONE;
    }

    int available(bool redo) const { return static_cast<int>((redo ? redo_ : undo_).size()); }
    void clear() { undo_.clear(); redo_.clear(); }

private:
    struct Entry {
        int tableId = -1;
        bool grid = false, chains = false, phrases = false, table = false;
        Parts before, after;
    };
    std::vector<Entry> undo_, redo_;

    static void chain_emit(songcore::JsonWriter& w, const songcore::Chain& c) { songcore::detail::emit_chain(w, c); }
    static void phrase_emit(songcore::JsonWriter& w, const songcore::Phrase& p) { songcore::detail::emit_phrase(w, p); }

    template <typename T, typename F>
    static std::string pool_text(const std::vector<T>& pool, F emit) {
        songcore::JsonWriter w{songcore::JsonLayout::Minified};
        w.begin_object();
        songcore::detail::emit_pool(w, "p", pool, emit);
        w.end_object();
        return w.out;
    }
    static std::string text(const songcore::Table& t) {
        songcore::JsonWriter w{songcore::JsonLayout::Minified};
        songcore::detail::emit_table(w, t);
        return w.out;
    }

    static bool matches(const songcore::Project& p, const Entry& e, const Parts& want) {
        const Parts now = take(p, e.tableId);
        if (e.table && text(now.table) != text(want.table)) return false;
        if (e.grid && now.grid != want.grid) return false;
        if (e.chains && pool_text(now.chains, chain_emit) != pool_text(want.chains, chain_emit)) return false;
        if (e.phrases && pool_text(now.phrases, phrase_emit) != pool_text(want.phrases, phrase_emit)) return false;
        return true;
    }

    static void restore(songcore::Project& p, const Entry& e, const Parts& state) {
        if (e.table && e.tableId >= 0 && e.tableId < static_cast<int>(p.tables.size()))
            p.tables[static_cast<size_t>(e.tableId)] = state.table;
        if (e.grid)
            for (size_t t = 0; t < p.tracks.size() && t < state.grid.size(); ++t) p.tracks[t].chainRefs = state.grid[t];
        if (e.chains) p.chains = state.chains;
        if (e.phrases) p.phrases = state.phrases;
    }
};

}  // namespace pt::ui
