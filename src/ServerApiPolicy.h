/***************************************************
    Copyright (C) 2026  WLANThermo gas fork contributors

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    HISTORY: Please refer Github History

****************************************************/
#pragma once

// Which parts of a server answer the device may act on (security review
// F-3, package S3). Upstream passed every 200 answer of any server (also the
// custom cloud) to the same parser: an "url" object rewrote the server hosts
// for good and "update.force" started an update without user action.
//  - answers of the custom cloud server are not parsed at all
//  - "update" only from the answer to an update request
//  - "url" (host rewrite) never from a server answer; local /setapi still
//    allows it (authenticated when a web password is set)

#include <stdint.h>

#define SERVER_ANSWER_PARSE 0x01u
#define SERVER_ANSWER_ALLOW_UPDATE 0x02u
#define SERVER_ANSWER_ALLOW_URL 0x04u
#define SERVER_ANSWER_ALL (SERVER_ANSWER_PARSE | SERVER_ANSWER_ALLOW_UPDATE | SERVER_ANSWER_ALLOW_URL)

inline uint8_t serverAnswerPolicy(bool isUpdateRequest, bool isCustomServer)
{
  if (isCustomServer)
    return 0u;

  return SERVER_ANSWER_PARSE | (isUpdateRequest ? SERVER_ANSWER_ALLOW_UPDATE : 0u);
}

// removes the parts of a parsed answer that the policy does not allow
template <typename TJsonObject>
inline void filterServerAnswer(TJsonObject &json, uint8_t policy)
{
  if (0u == (policy & SERVER_ANSWER_ALLOW_URL))
    json.remove("url");

  if (0u == (policy & SERVER_ANSWER_ALLOW_UPDATE))
    json.remove("update");
}
