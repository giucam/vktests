/*
 * Copyright 2015 Giulio Camuffo <giuliocamuffo@gmail.com>
 *
 * This file is part of Orbital
 *
 * Orbital is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Orbital is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Orbital.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ORBITAL_STRINGVIEW_H
#define ORBITAL_STRINGVIEW_H

#include <string>
#include <functional>
#include <iostream>

class stringview
{
public:
    stringview();
    stringview(const char *str);
    stringview(const char *str, size_t l);
    stringview(const std::string &str);

    inline bool isNull() const { return string == nullptr; }
    inline bool isEmpty() const { return string && size() == 0; }
    inline size_t size() const { return end - string; }

    std::string to_string() const;

    void split(char c, const std::function<bool (stringview substr)> &func) const;

    bool operator==(stringview v) const;
    inline bool operator!=(stringview v) const { return !(*this ==  v); }

private:
    const char *string;
    const char *end;

    friend std::ostream &operator<<(std::ostream &os, stringview v) {
        os.write(v.string, v.size());
        return os;
    }
};

inline bool operator==(const std::string &str, stringview v) { return stringview(str) == v; }
inline bool operator!=(const std::string &str, stringview v) { return stringview(str) != v; }

#endif
