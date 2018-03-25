/*
 *  Copyright (C) 2014 Florian Geyer <blueice@fobos.de>
 *  Copyright (C) 2017 KeePassXC Team <team@keepassxc.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 or (at your option)
 *  version 3 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "EntrySearcher.h"

#include "core/Group.h"

EntrySearcher::EntrySearcher(bool caseSensitive) :
    m_caseSensitive(caseSensitive)
{
}

QList<Entry*> EntrySearcher::search(const QString& searchString, const Group* group)
{
    QList<Entry*> results;

    if (group->resolveSearchingEnabled()) {
        results.append(searchEntries(searchString, group->entries()));
    }

    for (Group* childGroup : group->children()) {
        if (childGroup->resolveSearchingEnabled()) {
            results.append(searchEntries(searchString, childGroup->entries()));
        }
    }

    return results;
}

QList<Entry*> EntrySearcher::searchEntries(const QString& searchString, const QList<Entry*>& entries)
{
    QList<Entry*> results;
    for (Entry* entry : entries) {
       if (searchEntryImpl(searchString, entry)) {
           results.append(entry);
       }
    }
    return results;
}

void EntrySearcher::setCaseSensitive(bool state)
{
    m_caseSensitive = state;
}

bool EntrySearcher::searchEntryImpl(const QString& searchString, Entry* entry)
{
    auto searchTerms = parseSearchTerms(searchString);
    bool found;

    for (SearchTerm* term : searchTerms) {
        switch (term->field) {
        case Field::Title:
            found = term->regex.match(entry->resolvePlaceholder(entry->title())).hasMatch();
            break;
        case Field::Username:
            found = term->regex.match(entry->resolvePlaceholder(entry->username())).hasMatch();
            break;
        case Field::Password:
            found = term->regex.match(entry->resolvePlaceholder(entry->password())).hasMatch();
            break;
        case Field::Url:
            found = term->regex.match(entry->resolvePlaceholder(entry->url())).hasMatch();
            break;
        case Field::Notes:
            found = term->regex.match(entry->notes()).hasMatch();
            break;
        case Field::Attribute:
            found = !entry->attributes()->keys().filter(term->regex).empty();
            break;
        case Field::Attachment:
            found = !entry->attachments()->keys().filter(term->regex).empty();
            break;
        default:
            found = term->regex.match(entry->resolvePlaceholder(entry->title())).hasMatch() ||
                    term->regex.match(entry->resolvePlaceholder(entry->username())).hasMatch() ||
                    term->regex.match(entry->resolvePlaceholder(entry->url())).hasMatch() ||
                    term->regex.match(entry->notes()).hasMatch();
        }

        // Short circuit if we failed to match or we matched and are excluding this term
        if (!found || term->exclude) {
            return false;
        }
    }

    return true;
}

QList<EntrySearcher::SearchTerm*> EntrySearcher::parseSearchTerms(const QString& searchString)
{
    auto terms = QList<SearchTerm*>();
    // Group 1 = modifiers, Group 2 = field, Group 3 = quoted string, Group 4 = unquoted string
    auto termParser = QRegularExpression(R"re(([-*+]+)?(?:(\w*):)?(?:(?=")"((?:[^"\\]|\\.)*)"|([^ ]*))( |$))re");
    // Escape common regex symbols except for *, ?, and |
    auto regexEscape = QRegularExpression(R"re(([-[\]{}()+.,\\\/^$#]))re");

    auto results = termParser.globalMatch(searchString);
    while (results.hasNext()) {
        auto result = results.next();
        auto term = new SearchTerm();

        // Quoted string group
        term->word = result.captured(3);

        // If empty, use the unquoted string group
        if (term->word.isEmpty()) {
            term->word = result.captured(4);
        }

        // If still empty, ignore this match
        if (term->word.isEmpty()) {
            delete term;
            continue;
        }

        QString regex = term->word;

        // Wildcard support (*, ?, |)
        if (!result.captured(1).contains("*")) {
            regex.replace(regexEscape, "\\\\1");
            regex.replace("**", "*");
            regex.replace("*", ".*");
            regex.replace("?", ".");
        }

        term->regex = QRegularExpression(regex);
        if (!m_caseSensitive) {
            term->regex.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
        }

        // Exact modifier
        if (result.captured(1).contains("+")) {
            term->regex.setPattern("^" + term->regex.pattern() + "$");
        }

        // Exclude modifier
        term->exclude = result.captured(1).contains("-");

        // Determine the field to search
        QString field = result.captured(2);
        if (!field.isEmpty()) {
            auto cs = Qt::CaseInsensitive;
            if (field.compare("title", cs) == 0) {
                term->field = Field::Title;
            } else if (field.startsWith("user", cs)) {
                term->field = Field::Username;
            } else if (field.startsWith("pass", cs)) {
                term->field = Field::Password;
            } else if (field.compare("url", cs) == 0) {
                term->field = Field::Url;
            } else if (field.compare("notes", cs) == 0) {
                term->field = Field::Notes;
            } else if (field.startsWith("attr", cs)) {
                term->field = Field::Attribute;
            } else if (field.startsWith("attach", cs)) {
                term->field = Field::Attachment;
            } else {
                term->field = Field::All;
            }
        }

        terms.append(term);
    }

    return terms;
}
