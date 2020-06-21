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

#pragma once

#include "service.h"
#include "taiga/http.h"

namespace anime {
class Season;
}
namespace library {
struct QueueItem;
}

namespace sync {

bool AuthenticateUser();
void GetUser();
void GetLibraryEntries(const int offset = 0);
void GetMetadataById(int id);
void GetSeason(const anime::Season season, const int offset);
void SearchTitle(std::wstring title, int id);
void Synchronize();
void UpdateLibraryEntry(const library::QueueItem& queue_item);

void DownloadImage(int anime_id, const std::wstring& image_url);

bool AddAuthenticationToRequest(Request& request);
void AddPageOffsetToRequest(const int offset, Request& request);
bool AddServiceDataToRequest(Request& request, int id);
bool RequestNeedsAuthentication(RequestType request_type, ServiceId service_id);
void SetActiveServiceForRequest(Request& request);
bool UserAuthenticated();
void InvalidateUserAuthentication();
bool IsUserAccountAvailable();
bool IsUserAuthenticationAvailable();

bool ServiceSupportsRequestType(RequestType request_type);
RequestType ClientModeToRequestType(taiga::HttpClientMode client_mode);
taiga::HttpClientMode RequestTypeToClientMode(RequestType request_type);

}  // namespace sync
