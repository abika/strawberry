/*
 * Strawberry Music Player
 * This file was part of Clementine.
 * Copyright 2010, David Sansome <me@davidsansome.com>
 * Copyright 2018-2021, Jonas Kvinge <jonas@jkvinge.net>
 *
 * Strawberry is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Strawberry is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Strawberry.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include <QString>
#include <QChar>
#include <QStringList>
#include <QRegularExpression>
#include <QFileInfo>
#include <QValidator>

#include "utilities/filenameconstants.h"
#include "utilities/timeconstants.h"
#include "utilities/transliterate.h"
#include "core/song.h"

#include "organizeformat.h"
#include "organizeformatvalidator.h"

const char OrganizeFormat::kBlockPattern[] = "\\{([^{}]+)\\}";
const char OrganizeFormat::kTagPattern[] = "\\%([a-zA-Z]*)";

const QStringList OrganizeFormat::kKnownTags = QStringList() << QStringLiteral("title")
                                                             << QStringLiteral("album")
                                                             << QStringLiteral("artist")
                                                             << QStringLiteral("artistinitial")
                                                             << QStringLiteral("albumartist")
                                                             << QStringLiteral("composer")
                                                             << QStringLiteral("track")
                                                             << QStringLiteral("disc")
                                                             << QStringLiteral("year")
                                                             << QStringLiteral("originalyear")
                                                             << QStringLiteral("genre")
                                                             << QStringLiteral("comment")
                                                             << QStringLiteral("length")
                                                             << QStringLiteral("bitrate")
                                                             << QStringLiteral("samplerate")
                                                             << QStringLiteral("bitdepth")
                                                             << QStringLiteral("extension")
                                                             << QStringLiteral("performer")
                                                             << QStringLiteral("grouping")
                                                             << QStringLiteral("lyrics");

const QStringList OrganizeFormat::kUniqueTags = QStringList() << QStringLiteral("title")
                                                              << QStringLiteral("track");

OrganizeFormat::OrganizeFormat(const QString &format)
    : format_(format),
      remove_problematic_(false),
      remove_non_fat_(false),
      remove_non_ascii_(false),
      allow_ascii_ext_(false),
      replace_spaces_(true) {}

void OrganizeFormat::set_format(const QString &v) {
  format_ = v;
  format_.replace(QLatin1Char('\\'), QLatin1Char('/'));
}

bool OrganizeFormat::IsValid() const {

  int pos = 0;
  QString format_copy(format_);

  OrganizeFormatValidator v;
  return v.validate(format_copy, pos) == QValidator::Acceptable;

}

OrganizeFormat::GetFilenameForSongResult OrganizeFormat::GetFilenameForSong(const Song &song, QString extension) const {

  bool unique_filename = false;
  QString filepath = ParseBlock(format_, song, &unique_filename);

  if (filepath.isEmpty()) {
    filepath = song.basefilename();
  }

  {
    QFileInfo fileinfo(filepath);
    if (fileinfo.completeBaseName().isEmpty()) {
      // Avoid having empty filenames, or filenames with extension only: in this case, keep the original filename.
      // We remove the extension from "filename" if it exists, as song.basefilename() also contains the extension.
      QString path = fileinfo.path();
      filepath.clear();
      if (!path.isEmpty()) {
        filepath.append(path);
        if (path.right(1) != QLatin1Char('/')) {
          filepath.append(QLatin1Char('/'));
        }
      }
      filepath.append(song.basefilename());
    }
  }

  if (filepath.isEmpty() || (filepath.contains(QLatin1Char('/')) && (filepath.section(QLatin1Char('/'), 0, -2).isEmpty() || filepath.section(QLatin1Char('/'), 0, -2).isEmpty()))) {
    return GetFilenameForSongResult();
  }

  if (remove_problematic_) {
    static const QRegularExpression regex_problematic_characters(QLatin1String(kProblematicCharactersRegex), QRegularExpression::PatternOption::CaseInsensitiveOption);
    filepath = filepath.remove(regex_problematic_characters);
  }
  if (remove_non_fat_ || (remove_non_ascii_ && !allow_ascii_ext_)) filepath = Utilities::Transliterate(filepath);
  if (remove_non_fat_) {
    static const QRegularExpression regex_invalid_fat_characters(QLatin1String(kInvalidFatCharactersRegex), QRegularExpression::PatternOption::CaseInsensitiveOption);
    filepath = filepath.remove(regex_invalid_fat_characters);
  }

  if (remove_non_ascii_) {
    int ascii = 128;
    if (allow_ascii_ext_) ascii = 255;
    QString stripped;
    for (int i = 0; i < filepath.length(); ++i) {
      const QChar c = filepath[i];
      if (c.unicode() < ascii) {
        stripped.append(c);
      }
      else {
        const QString decomposition = c.decomposition();
        if (!decomposition.isEmpty() && decomposition[0].unicode() < ascii) {
          stripped.append(decomposition[0]);
        }
      }
    }
    filepath = stripped;
  }

  // Remove repeated whitespaces in the filepath.
  filepath = filepath.simplified();

  // Fixup extension
  QFileInfo info(filepath);
  filepath.clear();
  if (extension.isEmpty()) {
    if (info.suffix().isEmpty()) {
      extension = QFileInfo(song.url().toLocalFile()).suffix();
    }
    else {
      extension = info.suffix();
    }
  }
  if (!info.path().isEmpty() && info.path() != QLatin1Char('.')) {
    filepath.append(info.path());
    filepath.append(QLatin1Char('/'));
  }
  filepath.append(info.completeBaseName());

  // Fix any parts of the path that start with dots.
  QStringList parts_old = filepath.split(QLatin1Char('/'));
  QStringList parts_new;
  for (int i = 0; i < parts_old.count(); ++i) {
    QString part = parts_old[i];
    for (int j = 0; j < kInvalidPrefixCharactersCount; ++j) {
      if (part.startsWith(QLatin1Char(kInvalidPrefixCharacters[j]))) {
        part = part.remove(0, 1);
        break;
      }
    }
    part = part.trimmed();
    parts_new.append(part);
  }
  filepath = parts_new.join(QLatin1Char('/'));

  if (replace_spaces_) {
    static const QRegularExpression regex_whitespaces(QStringLiteral("\\s"));
    filepath.replace(regex_whitespaces, QStringLiteral("_"));
  }

  if (!extension.isEmpty()) {
    filepath.append(QStringLiteral(".%1").arg(extension));
  }

  return GetFilenameForSongResult(filepath, unique_filename);

}

QString OrganizeFormat::ParseBlock(QString block, const Song &song, bool *have_tagdata, bool *any_empty) const {

  // Find any blocks first
  qint64 pos = 0;
  static const QRegularExpression block_regexp(QString::fromLatin1(kBlockPattern));
  QRegularExpressionMatch re_match;
  for (re_match = block_regexp.match(block, pos); re_match.hasMatch(); re_match = block_regexp.match(block, pos)) {
    pos = re_match.capturedStart();
    // Recursively parse the block
    bool empty = false;
    QString value = ParseBlock(re_match.captured(1), song, have_tagdata, &empty);
    if (empty) value = QLatin1String("");

    // Replace the block's value
    block.replace(pos, re_match.capturedLength(), value);
    pos += value.length();
  }

  // Now look for tags
  bool empty = false;
  pos = 0;
  static const QRegularExpression tag_regexp(QString::fromLatin1(kTagPattern));
  for (re_match = tag_regexp.match(block, pos); re_match.hasMatch(); re_match = tag_regexp.match(block, pos)) {
    pos = re_match.capturedStart();
    const QString tag = re_match.captured(1);
    const QString value = TagValue(tag, song);
    if (value.isEmpty()) {
      empty = true;
    }
    else if (have_tagdata && kUniqueTags.contains(tag)) {
      *have_tagdata = true;
    }

    block.replace(pos, re_match.capturedLength(), value);
    pos += value.length();
  }

  if (any_empty) {
    *any_empty = empty;
  }

  return block;

}

QString OrganizeFormat::TagValue(const QString &tag, const Song &song) const {

  QString value;

  if (tag == QLatin1String("title")) {
    value = song.title();
  }
  else if (tag == QLatin1String("album")) {
    value = song.album();
  }
  else if (tag == QLatin1String("artist")) {
    value = song.artist();
  }
  else if (tag == QLatin1String("composer")) {
    value = song.composer();
  }
  else if (tag == QLatin1String("performer")) {
    value = song.performer();
  }
  else if (tag == QLatin1String("grouping")) {
    value = song.grouping();
  }
  else if (tag == QLatin1String("lyrics")) {
    value = song.lyrics();
  }
  else if (tag == QLatin1String("genre")) {
    value = song.genre();
  }
  else if (tag == QLatin1String("comment")) {
    value = song.comment();
  }
  else if (tag == QLatin1String("year")) {
    value = QString::number(song.year());
  }
  else if (tag == QLatin1String("originalyear")) {
    value = QString::number(song.effective_originalyear());
  }
  else if (tag == QLatin1String("track")) {
    value = QString::number(song.track());
  }
  else if (tag == QLatin1String("disc")) {
    value = QString::number(song.disc());
  }
  else if (tag == QLatin1String("length")) {
    value = QString::number(song.length_nanosec() / kNsecPerSec);
  }
  else if (tag == QLatin1String("bitrate")) {
    value = QString::number(song.bitrate());
  }
  else if (tag == QLatin1String("samplerate")) {
    value = QString::number(song.samplerate());
  }
  else if (tag == QLatin1String("bitdepth")) {
    value = QString::number(song.bitdepth());
  }
  else if (tag == QLatin1String("extension")) {
    value = QFileInfo(song.url().toLocalFile()).suffix();
  }
  else if (tag == QLatin1String("artistinitial")) {
    value = song.effective_albumartist().trimmed();
    if (!value.isEmpty()) {
      static const QRegularExpression regex_the(QStringLiteral("^the\\s+"), QRegularExpression::CaseInsensitiveOption);
      value = value.remove(regex_the);
      value = value[0].toUpper();
    }
  }
  else if (tag == QLatin1String("albumartist")) {
    value = song.is_compilation() ? QStringLiteral("Various Artists") : song.effective_albumartist();
  }

  if (value == QLatin1Char('0') || value == QLatin1String("-1")) value = QLatin1String("");

  // Prepend a 0 to single-digit track numbers
  if (tag == QLatin1String("track") && value.length() == 1) value.prepend(QLatin1Char('0'));

  // Replace characters that really shouldn't be in paths
  static const QRegularExpression regex_invalid_dir_characters(QString::fromLatin1(kInvalidDirCharactersRegex), QRegularExpression::PatternOption::CaseInsensitiveOption);
  value = value.remove(regex_invalid_dir_characters);
  if (remove_problematic_) value = value.remove(QLatin1Char('.'));
  value = value.trimmed();

  return value;

}
