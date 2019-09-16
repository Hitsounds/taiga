/*
** Taiga
** Copyright (C) 2010-2019, Eren Okka
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "base/log.h"
#include "base/string.h"
#include "base/xml.h"
#include "media/anime_db.h"
#include "media/anime_util.h"
#include "media/library/history.h"
#include "sync/sync.h"
#include "taiga/announce.h"
#include "taiga/path.h"
#include "taiga/settings.h"
#include "taiga/version.h"
#include "track/media.h"
#include "track/scanner.h"
#include "ui/ui.h"

History::History() {  
  queue.history = this;
}

void History::Clear(bool save) {
  items.clear();

  ui::OnHistoryChange();

  if (save)
    Save();
}

bool History::Load() {
  items.clear();
  queue.items.clear();

  XmlDocument document;
  const auto path = taiga::GetPath(taiga::Path::UserHistory);
  const auto parse_result = XmlLoadFileToDocument(document, path);

  if (!parse_result)
    return false;

  // Meta
  const auto meta_version = XmlReadMetaVersion(document);
  const semaver::Version version(WstrToStr(meta_version));

  // Items
  auto node_items = document.child(L"history").child(L"items");
  for (auto item : node_items.children(L"item")) {
    HistoryItem history_item;
    history_item.anime_id = item.attribute(L"anime_id").as_int(anime::ID_NOTINLIST);
    history_item.episode = item.attribute(L"episode").as_int();
    history_item.time = item.attribute(L"time").value();

    if (anime::db.Find(history_item.anime_id)) {
      items.push_back(history_item);
    } else {
      LOGW(L"Item does not exist in the database.\n"
           L"ID: {}\nEpisode: {}\nTime: {}",
           history_item.anime_id, *history_item.episode, history_item.time);
    }
  }
  // Queue events
  if (version < semaver::Version(1, 1, 4)) {
    ReadQueueInCompatibilityMode(document);
  } else {
    ReadQueue(document);
    HandleCompatibility(meta_version);
  }

  return true;
}

void History::ReadQueue(const pugi::xml_document& document) {
  auto node_queue = document.child(L"history").child(L"queue");

  for (auto item : node_queue.children(L"item")) {
    HistoryItem history_item;

    history_item.anime_id = item.attribute(L"anime_id").as_int(anime::ID_NOTINLIST);
    history_item.mode = TranslateModeFromString(item.attribute(L"mode").value());
    history_item.time = item.attribute(L"time").value();

    #define READ_ATTRIBUTE_BOOL(x, y) \
        if (!item.attribute(y).empty()) x = item.attribute(y).as_bool();
    #define READ_ATTRIBUTE_INT(x, y) \
        if (!item.attribute(y).empty()) x = item.attribute(y).as_int();
    #define READ_ATTRIBUTE_STR(x, y) \
        if (!item.attribute(y).empty()) x = item.attribute(y).as_string();
    #define READ_ATTRIBUTE_DATE(x, y) \
        if (!item.attribute(y).empty()) x = (Date)item.attribute(y).as_string();

    READ_ATTRIBUTE_INT(history_item.episode, L"episode");
    READ_ATTRIBUTE_INT(history_item.score, L"score");
    READ_ATTRIBUTE_INT(history_item.status, L"status");
    READ_ATTRIBUTE_BOOL(history_item.enable_rewatching, L"enable_rewatching");
    READ_ATTRIBUTE_INT(history_item.rewatched_times, L"rewatched_times");
    READ_ATTRIBUTE_STR(history_item.tags, L"tags");
    READ_ATTRIBUTE_STR(history_item.notes, L"notes");
    READ_ATTRIBUTE_DATE(history_item.date_start, L"date_start");
    READ_ATTRIBUTE_DATE(history_item.date_finish, L"date_finish");

    #undef READ_ATTRIBUTE_DATE
    #undef READ_ATTRIBUTE_STR
    #undef READ_ATTRIBUTE_INT

    if (anime::db.Find(history_item.anime_id)) {
      queue.Add(history_item, false);
    } else {
      LOGW(L"Item does not exist in the database.\n"
           L"ID: {}", history_item.anime_id);
    }
  }
}

bool History::Save() {
  XmlDocument document;

  // Write meta
  XmlWriteMetaVersion(document, StrToWstr(taiga::version().to_string()));

  auto node_history = document.append_child(L"history");

  // Write items
  auto node_items = node_history.append_child(L"items");
  for (const auto& history_item : items) {
    auto node_item = node_items.append_child(L"item");
    node_item.append_attribute(L"anime_id") = history_item.anime_id;
    node_item.append_attribute(L"episode") = *history_item.episode;
    node_item.append_attribute(L"time") = history_item.time.c_str();
  }
  // Write queue
  auto node_queue = node_history.append_child(L"queue");
  for (const auto& history_item : queue.items) {
    auto node_item = node_queue.append_child(L"item");
    #define APPEND_ATTRIBUTE(x, y) \
        if (y) node_item.append_attribute(x) = *y;
    #define APPEND_ATTRIBUTE_STR(x, y) \
        if (y) node_item.append_attribute(x) = (*y).c_str();
    #define APPEND_ATTRIBUTE_DATE(x, y) \
        if (y) node_item.append_attribute(x) = (*y).to_string().c_str();
    node_item.append_attribute(L"anime_id") = history_item.anime_id;
    node_item.append_attribute(L"mode") = TranslateModeToString(history_item.mode).c_str();
    node_item.append_attribute(L"time") = history_item.time.c_str();
    APPEND_ATTRIBUTE(L"episode", history_item.episode);
    APPEND_ATTRIBUTE(L"score", history_item.score);
    APPEND_ATTRIBUTE(L"status", history_item.status);
    APPEND_ATTRIBUTE(L"enable_rewatching", history_item.enable_rewatching);
    APPEND_ATTRIBUTE(L"rewatched_times", history_item.rewatched_times);
    APPEND_ATTRIBUTE_STR(L"tags", history_item.tags);
    APPEND_ATTRIBUTE_STR(L"notes", history_item.notes);
    APPEND_ATTRIBUTE_DATE(L"date_start", history_item.date_start);
    APPEND_ATTRIBUTE_DATE(L"date_finish", history_item.date_finish);
    #undef APPEND_ATTRIBUTE_DATE
    #undef APPEND_ATTRIBUTE_STR
    #undef APPEND_ATTRIBUTE
  }

  const auto path = taiga::GetPath(taiga::Path::UserHistory);
  return XmlSaveDocumentToFile(document, path);
}

int History::TranslateModeFromString(const std::wstring& mode) {
  if (mode == L"add") {
    return taiga::kHttpServiceAddLibraryEntry;
  } else if (mode == L"delete") {
    return taiga::kHttpServiceDeleteLibraryEntry;
  } else {
    return taiga::kHttpServiceUpdateLibraryEntry;
  }
}

std::wstring History::TranslateModeToString(int mode) {
  switch (mode) {
    case taiga::kHttpServiceAddLibraryEntry:
      return L"add";
    case taiga::kHttpServiceDeleteLibraryEntry:
      return L"delete";
    default:
    case taiga::kHttpServiceUpdateLibraryEntry:
      return L"update";
  }
}
