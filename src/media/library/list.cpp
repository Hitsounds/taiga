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

#include "media/anime_db.h"

#include "base/log.h"
#include "base/string.h"
#include "base/xml.h"
#include "media/anime_util.h"
#include "media/library/queue.h"
#include "sync/service.h"
#include "taiga/http.h"
#include "taiga/path.h"
#include "taiga/settings.h"
#include "taiga/version.h"
#include "ui/ui.h"

namespace anime {

bool Database::LoadList() {
  ClearUserData();

  if (taiga::GetCurrentUsername().empty())
    return false;

  XmlDocument document;
  const auto path = taiga::GetPath(taiga::Path::UserLibrary);
  const auto parse_result = XmlLoadFileToDocument(document, path);

  if (!parse_result) {
    if (parse_result.status != pugi::status_file_not_found) {
      ui::DisplayErrorMessage(L"Could not read anime list.", path);
    }
    return false;
  }

  const auto meta_version = XmlReadMetaVersion(document);

  auto node_database = document.child(L"database");
  ReadDatabaseNode(node_database);

  auto node_library = document.child(L"library");
  for (auto node : node_library.children(L"anime")) {
    Item anime_item;
    anime_item.SetId(XmlReadStr(node, L"id"), sync::kTaiga);
    anime_item.SetSource(sync::kTaiga);

    anime_item.AddtoUserList();
    anime_item.SetMyId(XmlReadStr(node, L"library_id"));
    anime_item.SetMyLastWatchedEpisode(XmlReadInt(node, L"progress"));
    anime_item.SetMyDateStart(XmlReadStr(node, L"date_start"));
    anime_item.SetMyDateEnd(XmlReadStr(node, L"date_end"));
    anime_item.SetMyScore(XmlReadInt(node, L"score"));
    anime_item.SetMyStatus(XmlReadInt(node, L"status"));
    anime_item.SetMyRewatchedTimes(XmlReadInt(node, L"rewatched_times"));
    anime_item.SetMyRewatching(XmlReadInt(node, L"rewatching"));
    anime_item.SetMyRewatchingEp(XmlReadInt(node, L"rewatching_ep"));
    anime_item.SetMyTags(XmlReadStr(node, L"tags"));
    anime_item.SetMyNotes(XmlReadStr(node, L"notes"));
    anime_item.SetMyLastUpdated(XmlReadStr(node, L"last_updated"));

    UpdateItem(anime_item);
  }

  HandleListCompatibility(meta_version);

  return true;
}

bool Database::SaveList(bool include_database) const {
  if (items.empty())
    return false;

  XmlDocument document;

  XmlWriteMetaVersion(document, StrToWstr(taiga::version().to_string()));

  if (include_database) {
    WriteDatabaseNode(XmlChild(document, L"database"));
  }

  auto node_library = document.append_child(L"library");

  for (const auto& [id, item] : items) {
    if (item.IsInList()) {
      auto node = node_library.append_child(L"anime");
      XmlWriteInt(node, L"id", item.GetId());
      XmlWriteStr(node, L"library_id", item.GetMyId());
      XmlWriteInt(node, L"progress", item.GetMyLastWatchedEpisode(false));
      XmlWriteStr(node, L"date_start", item.GetMyDateStart().to_string());
      XmlWriteStr(node, L"date_end", item.GetMyDateEnd().to_string());
      XmlWriteInt(node, L"score", item.GetMyScore(false));
      XmlWriteInt(node, L"status", item.GetMyStatus(false));
      XmlWriteInt(node, L"rewatched_times", item.GetMyRewatchedTimes());
      XmlWriteInt(node, L"rewatching", item.GetMyRewatching(false));
      XmlWriteInt(node, L"rewatching_ep", item.GetMyRewatchingEp());
      XmlWriteStr(node, L"tags", item.GetMyTags(false));
      XmlWriteStr(node, L"notes", item.GetMyNotes(false));
      XmlWriteStr(node, L"last_updated", item.GetMyLastUpdated());
    }
  }

  const auto path = taiga::GetPath(taiga::Path::UserLibrary);
  return XmlSaveDocumentToFile(document, path);
}

////////////////////////////////////////////////////////////////////////////////

// @TODO: Move to util
int Database::GetItemCount(int status, bool check_history) {
  // Get current count
  int count = 0;
  for (const auto& it : items) {
    const auto& item = it.second;
    if (item.GetMyRewatching()) {
      if (status == kWatching)
        ++count;
    } else {
      if (item.GetMyStatus(false) == status)
        ++count;
    }
  }

  // Search queued items for status changes
  if (check_history) {
    for (const auto& queue_item : library::queue.items) {
      if (queue_item.status ||
          queue_item.mode == taiga::kHttpServiceDeleteLibraryEntry) {
        if (status == *queue_item.status) {
          count++;
        } else {
          auto anime_item = Find(queue_item.anime_id);
          if (anime_item && status == anime_item->GetMyStatus(false))
            count--;
        }
      }
    }
  }

  return count;
}

////////////////////////////////////////////////////////////////////////////////

void Database::AddToList(int anime_id, int status) {
  auto anime_item = Find(anime_id);

  if (!anime_item || anime_item->IsInList())
    return;

  if (taiga::GetCurrentUsername().empty()) {
    ui::ChangeStatusText(
        L"Please set up your account before adding anime to your list.");
    return;
  }

  if (status == anime::kUnknownStatus)
    status = anime::IsAiredYet(*anime_item) ? anime::kWatching :
                                              anime::kPlanToWatch;

  anime_item->AddtoUserList();

  library::QueueItem queue_item;
  queue_item.anime_id = anime_id;
  queue_item.status = status;
  if (status == anime::kCompleted) {
    queue_item.episode = anime_item->GetEpisodeCount();
    if (anime_item->GetEpisodeCount() == 1)
      queue_item.date_start = GetDate();
    queue_item.date_finish = GetDate();
  }
  queue_item.mode = taiga::kHttpServiceAddLibraryEntry;
  library::queue.Add(queue_item);

  SaveDatabase();
  SaveList();

  ui::OnLibraryEntryAdd(anime_id);

  if (CurrentEpisode.anime_id == anime::ID_NOTINLIST)
    CurrentEpisode.Set(anime::ID_UNKNOWN);
}

void Database::ClearUserData() {
  for (auto& [id, item] : items) {
    item.RemoveFromUserList();
  }
}

bool Database::DeleteListItem(int anime_id) {
  auto anime_item = Find(anime_id);

  if (!anime_item)
    return false;
  if (!anime_item->IsInList())
    return false;

  anime_item->RemoveFromUserList();

  ui::ChangeStatusText(L"Item deleted. (" + anime::GetPreferredTitle(*anime_item) + L")");
  ui::OnLibraryEntryDelete(anime_item->GetId());

  if (CurrentEpisode.anime_id == anime_id)
    CurrentEpisode.Set(anime::ID_NOTINLIST);

  return true;
}

void Database::UpdateItem(const library::QueueItem& queue_item) {
  auto anime_item = Find(queue_item.anime_id);

  if (!anime_item)
    return;

  anime_item->AddtoUserList();

  // Edit episode
  if (queue_item.episode) {
    anime_item->SetMyLastWatchedEpisode(*queue_item.episode);
  }
  // Edit score
  if (queue_item.score) {
    anime_item->SetMyScore(*queue_item.score);
  }
  // Edit status
  if (queue_item.status) {
    anime_item->SetMyStatus(*queue_item.status);
  }
  // Edit rewatching status
  if (queue_item.enable_rewatching) {
    anime_item->SetMyRewatching(*queue_item.enable_rewatching);
  }
  if (queue_item.rewatched_times) {
    anime_item->SetMyRewatchedTimes(*queue_item.rewatched_times);
  }
  // Edit tags
  if (queue_item.tags) {
    anime_item->SetMyTags(*queue_item.tags);
  }
  // Edit notes
  if (queue_item.notes) {
    anime_item->SetMyNotes(*queue_item.notes);
  }
  // Edit dates
  if (queue_item.date_start) {
    anime_item->SetMyDateStart(*queue_item.date_start);
  }
  if (queue_item.date_finish) {
    anime_item->SetMyDateEnd(*queue_item.date_finish);
  }
  // Delete
  if (queue_item.mode == taiga::kHttpServiceDeleteLibraryEntry) {
    DeleteListItem(anime_item->GetId());
  }

  if (queue_item.mode != taiga::kHttpServiceDeleteLibraryEntry)
    anime::SetMyLastUpdateToNow(*anime_item);

  ui::OnLibraryEntryChange(queue_item.anime_id);
}

}  // namespace anime
