/*
 *      Copyright (C) 2012-2013 Team XBMC
 *      http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "GroupUtils.h"

#include <map>
#include <set>

#include "FileItem.h"
#include "filesystem/MultiPathDirectory.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "video/VideoDbUrl.h"
#include "video/VideoInfoTag.h"

using SetMap = std::map<int, std::set<CFileItemPtr> >;
using MovieMap = std::map<std::string, std::set<CFileItemPtr> >;
using EpisodeMap = std::map<std::string, std::set<CFileItemPtr> >;

using CFileItemListPtr = std::shared_ptr<CFileItemList>;

static void combine_entries(CFileItemListPtr &grouped, MovieMap::const_iterator &set) {
  
  CVideoInfoTag* groupInfo = grouped.get()->GetVideoInfoTag();

  int ratings = 0;

  groupInfo->m_playCount = 0;
  groupInfo->m_fRating = 0.0f;
  groupInfo->m_type = MediaTypeVideoCollection;

  for (std::set<CFileItemPtr>::const_iterator item = set->second.begin(); item != set->second.end(); item++)
  {
    grouped->Add(*item);
    CVideoInfoTag* itemInfo = (*item)->GetVideoInfoTag();

    // handle rating
    if (itemInfo->m_fRating > 0.0f)
    {
      ratings++;
      groupInfo->m_fRating += itemInfo->m_fRating;
    }

    // handle lastplayed
    if (itemInfo->m_lastPlayed.IsValid() && itemInfo->m_lastPlayed > groupInfo->m_lastPlayed)
      groupInfo->m_lastPlayed = itemInfo->m_lastPlayed;

    // handle dateadded
    if (itemInfo->m_dateAdded.IsValid() && itemInfo->m_dateAdded > groupInfo->m_dateAdded)
      groupInfo->m_dateAdded = itemInfo->m_dateAdded;

    // handle playcount/watched
    groupInfo->m_playCount += itemInfo->m_playCount;
  }

  if (ratings > 1)
    groupInfo->m_fRating /= ratings;

  /* TODO what makes sense here? */
  grouped->SetProperty("total", (int)set->second.size());
  grouped->SetProperty("watched", groupInfo->m_playCount);
  grouped->SetProperty("unwatched", (int)set->second.size() /*- iWatched*/);
  grouped->SetOverlayImage(CGUIListItem::ICON_OVERLAY_UNWATCHED, groupInfo->m_playCount > 0);
  grouped->m_bIsFolder = true;

  /* XXX add this in properly TODO */
  if (grouped->GetProperty("contextmenulabel(0)").isNull()) {
    grouped->SetProperty("contextmenulabel(0)", "List All Duplicates");
    grouped->SetProperty("contextmenuaction(0)", "ActivateWindow(Videos, " + grouped->GetPath() + ")");
  }

#if 0
  grouped->SetLabel("Loading Duplicates...");
  grouped->GetVideoInfoTag()->m_strTitle = "Loading Duplicates...";
  if (set->second.begin()->get()->GetVideoInfoTag()->m_strSortTitle.empty())
    grouped->GetVideoInfoTag()->m_strSortTitle = set->second.begin()->get()->GetVideoInfoTag()->m_strTitle;
  else
    grouped->GetVideoInfoTag()->m_strSortTitle = set->second.begin()->get()->GetVideoInfoTag()->m_strSortTitle;
#endif
}

bool GroupUtils::Group(GroupBy groupBy, const std::string &baseDir, const CFileItemList &items, CFileItemList &groupedItems, GroupAttribute groupAttributes /* = GroupAttributeNone */)
{
  CFileItemList ungroupedItems;
  return Group(groupBy, baseDir, items, groupedItems, ungroupedItems, groupAttributes);
}

bool GroupUtils::Group(GroupBy groupBy, const std::string &baseDir, const CFileItemList &items, CFileItemList &groupedItems, CFileItemList &ungroupedItems, GroupAttribute groupAttributes /* = GroupAttributeNone */)
{
  if (groupBy == GroupByNone)
    return false;

  // nothing to do if there are no items to group
  if (items.Size() <= 0)
    return true;

  SetMap setMap;
  MovieMap movieMap;
  EpisodeMap episodeMap;
  for (int index = 0; index < items.Size(); index++)
  {
    bool ungrouped = true;
    const CFileItemPtr item = items.Get(index);

    // group by sets
    if ((groupBy & GroupBySet) &&
      item->HasVideoInfoTag() && item->GetVideoInfoTag()->m_iSetId > 0)
    {
      ungrouped = false;
      setMap[item->GetVideoInfoTag()->m_iSetId].insert(item);
    }
    else if (groupBy & GroupByMovie &&
      item->HasVideoInfoTag() && !item->GetVideoInfoTag()->m_strIMDBNumber.empty())
    {
      ungrouped = false;
      movieMap[item->GetVideoInfoTag()->m_strIMDBNumber].insert(item);
    }
    else if (groupBy & GroupByEpisode &&
      item->HasVideoInfoTag() && !item->GetVideoInfoTag()->m_strUniqueId.empty())
    {
      ungrouped = false;
      episodeMap[item->GetVideoInfoTag()->m_strUniqueId].insert(item);
    }

    if (ungrouped)
      ungroupedItems.Add(item);
  }

  if ((groupBy & GroupBySet) && !setMap.empty())
  {
    CVideoDbUrl itemsUrl;
    if (!itemsUrl.FromString(baseDir))
      return false;

    for (SetMap::const_iterator set = setMap.begin(); set != setMap.end(); ++set)
    {
      // only one item in the set, so add it to the ungrouped items
      if (set->second.size() == 1 && (groupAttributes & GroupAttributeIgnoreSingleItems))
      {
        ungroupedItems.Add(*set->second.begin());
        continue;
      }

      CFileItemPtr pItem(new CFileItem((*set->second.begin())->GetVideoInfoTag()->m_strSet));
      pItem->GetVideoInfoTag()->m_iDbId = set->first;
      pItem->GetVideoInfoTag()->m_type = MediaTypeVideoCollection;

      std::string basePath = StringUtils::Format("videodb://movies/sets/%i/", set->first);
      CVideoDbUrl videoUrl;
      if (!videoUrl.FromString(basePath))
        pItem->SetPath(basePath);
      else
      {
        videoUrl.AddOptions(itemsUrl.GetOptionsString());
        pItem->SetPath(videoUrl.ToString());
      }
      pItem->m_bIsFolder = true;

      CVideoInfoTag* setInfo = pItem->GetVideoInfoTag();
      setInfo->m_strPath = pItem->GetPath();
      setInfo->m_strTitle = pItem->GetLabel();
      setInfo->m_strPlot = (*set->second.begin())->GetVideoInfoTag()->m_strSetOverview;

      int ratings = 0;
      int iWatched = 0; // have all the movies been played at least once?
      std::set<std::string> pathSet;
      for (std::set<CFileItemPtr>::const_iterator movie = set->second.begin(); movie != set->second.end(); ++movie)
      {
        CVideoInfoTag* movieInfo = (*movie)->GetVideoInfoTag();
        // handle rating
        if (movieInfo->m_fRating > 0.0f)
        {
          ratings++;
          setInfo->m_fRating += movieInfo->m_fRating;
        }

        // handle year
        if (movieInfo->m_iYear > setInfo->m_iYear)
          setInfo->m_iYear = movieInfo->m_iYear;

        // handle lastplayed
        if (movieInfo->m_lastPlayed.IsValid() && movieInfo->m_lastPlayed > setInfo->m_lastPlayed)
          setInfo->m_lastPlayed = movieInfo->m_lastPlayed;

        // handle dateadded
        if (movieInfo->m_dateAdded.IsValid() && movieInfo->m_dateAdded > setInfo->m_dateAdded)
          setInfo->m_dateAdded = movieInfo->m_dateAdded;

        // handle playcount/watched
        setInfo->m_playCount += movieInfo->m_playCount;
        if (movieInfo->m_playCount > 0)
          iWatched++;

        //accumulate the path for a multipath construction
        CFileItem video(movieInfo->m_basePath, false);
        if (video.IsVideo())
          pathSet.insert(URIUtils::GetParentPath(movieInfo->m_basePath));
        else
          pathSet.insert(movieInfo->m_basePath);
      }
      setInfo->m_basePath = XFILE::CMultiPathDirectory::ConstructMultiPath(pathSet);

      if (ratings > 1)
        pItem->GetVideoInfoTag()->m_fRating /= ratings;

      setInfo->m_playCount = iWatched >= (int)set->second.size() ? (setInfo->m_playCount / set->second.size()) : 0;
      pItem->SetProperty("total", (int)set->second.size());
      pItem->SetProperty("watched", iWatched);
      pItem->SetProperty("unwatched", (int)set->second.size() - iWatched);
      pItem->SetOverlayImage(CGUIListItem::ICON_OVERLAY_UNWATCHED, setInfo->m_playCount > 0);

      groupedItems.Add(pItem);
    }
  }

  if ((groupBy & GroupByMovie) && movieMap.size() > 0)
  {
    CVideoDbUrl itemsUrl;
    if (!itemsUrl.FromString(baseDir))
      return false;

    for (MovieMap::const_iterator set = movieMap.begin(); set != movieMap.end(); set++)
    {

      // only one copy of the movie, so just re-add it
      if (set->second.size() == 1)
      {
        ungroupedItems.Add(*set->second.begin());
        continue;
      }

      CFileItemListPtr pItem(new CFileItemList());

      /* Set the video information this ensure sorting and naming works correctly
      * it will overwrite path so we do this first */
      pItem->SetFromVideoInfoTag(*set->second.begin()->get()->GetVideoInfoTag());

      /* Zero out the paths, we will fill these in the background VideoThumbLoader */
      pItem->GetVideoInfoTag()->m_basePath = "";
      pItem->GetVideoInfoTag()->m_strFileNameAndPath = "";

      std::string basePath = "videodb://movies/titles/";
      CVideoDbUrl videoUrl;
      if (!videoUrl.FromString(basePath)) {
        pItem->SetPath(basePath);
      }
      else {
        videoUrl.AddOptions(itemsUrl.GetOptionsString());
        videoUrl.AddOption("imbdid", set->first);
        pItem->SetPath(videoUrl.ToString());
      }

      /* Add all item to the list and merge together play counts etc. */
      combine_entries(pItem, set);
      groupedItems.Add(pItem);
    }
  }

  if ((groupBy & GroupByEpisode) && episodeMap.size() > 0)
  {
    CVideoDbUrl itemsUrl;
    if (!itemsUrl.FromString(baseDir))
      return false;

    for (EpisodeMap::const_iterator set = episodeMap.begin(); set != episodeMap.end(); set++)
    {

      // only one copy of the episode, so just re-add it
      if (set->second.size() == 1)
      {
        ungroupedItems.Add(*set->second.begin());
        continue;
      }

      CFileItemListPtr pItem(new CFileItemList());

      /* Set the video information this ensure sorting and naming works correctly
       * it will overwrite path so we do this first */
      pItem->SetFromVideoInfoTag(*set->second.begin()->get()->GetVideoInfoTag());

      /* Zero out the paths, we will fill these in the background VideoThumbLoader */
      pItem->GetVideoInfoTag()->m_basePath = "";
      pItem->GetVideoInfoTag()->m_strFileNameAndPath = "";

      std::string basePath = baseDir;
      CVideoDbUrl videoUrl;
      if (!videoUrl.FromString(baseDir))
        pItem->SetPath(basePath);
      else
      {
        videoUrl.AddOptions(itemsUrl.GetOptionsString());
        videoUrl.AddOption("tvepisodenumber", set->first);
        pItem->SetPath(videoUrl.ToString());
      }

      /* Add all item to the list and merge together play counts etc. */
      combine_entries(pItem, set);
      groupedItems.Add(pItem);
    }
  }
  return true;
}

bool GroupUtils::GroupAndMix(GroupBy groupBy, const std::string &baseDir, const CFileItemList &items, CFileItemList &groupedItemsMixed, GroupAttribute groupAttributes /* = GroupAttributeNone */)
{
  CFileItemList ungroupedItems;
  if (!Group(groupBy, baseDir, items, groupedItemsMixed, ungroupedItems, groupAttributes))
    return false;

  // add all the ungrouped items as well
  groupedItemsMixed.Append(ungroupedItems);

  return true;
}
