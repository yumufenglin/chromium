# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from HTMLParser import HTMLParser
import logging
import re

from docs_server_utils import FormatKey
from file_system import FileNotFoundError
from third_party.handlebar import Handlebar

class _IntroParser(HTMLParser):
  """ An HTML parser which will parse table of contents and page title info out
  of an intro.
  """
  def __init__(self):
    HTMLParser.__init__(self)
    self.toc = []
    self.page_title = None
    self._recent_tag = None
    self._current_heading = {}

  def handle_starttag(self, tag, attrs):
    id_ = ''
    if tag not in ['h1', 'h2', 'h3']:
      return
    if tag != 'h1' or self.page_title is None:
      self._recent_tag = tag
    for attr in attrs:
      if attr[0] == 'id':
        id_ = attr[1]
    if tag == 'h2':
      self._current_heading = { 'link': id_, 'subheadings': [], 'title': '' }
      self.toc.append(self._current_heading)
    elif tag == 'h3':
      self._current_heading = { 'link': id_, 'title': '' }
      self.toc[-1]['subheadings'].append(self._current_heading)

  def handle_endtag(self, tag):
    if tag in ['h1', 'h2', 'h3']:
      self._recent_tag = None

  def handle_data(self, data):
    if self._recent_tag is None:
      return
    if self._recent_tag == 'h1':
      if self.page_title is None:
        self.page_title = data
      else:
        self.page_title += data
    elif self._recent_tag in ['h2', 'h3']:
      self._current_heading['title'] += data

class IntroDataSource(object):
  """This class fetches the intros for a given API. From this intro, a table
  of contents dictionary is created, which contains the headings in the intro.
  """
  def __init__(self, cache_builder, base_paths):
    self._cache = cache_builder.build(self._MakeIntroDict)
    self._base_paths = base_paths
    self._intro_regex = re.compile('<h1[^>.]*?>.*?</h1>', flags=re.DOTALL)

  def _MakeIntroDict(self, intro):
    parser = _IntroParser()
    parser.feed(intro)
    intro = re.sub(self._intro_regex, '', intro, count=1)
    return {
      'intro': Handlebar(intro),
      'toc': parser.toc,
      'title': parser.page_title
    }

  def __getitem__(self, key):
    return self.get(key)

  def get(self, key):
    real_path = FormatKey(key)
    error = None
    for base_path in self._base_paths:
      try:
        return self._cache.GetFromFile(base_path + '/' + real_path)
      except FileNotFoundError as error:
        pass
    raise ValueError(str(error) + ': No intro found for "%s".' % key)
