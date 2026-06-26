#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#ifdef _WIN32
#include <conio.h>
#include <io.h>
#endif

#include "bit.h"
#include "tec2.h"

using namespace std;

namespace {

struct App {
    static constexpr int OP_MADD = 53;
    static constexpr int OP_JNE = 54;
    static constexpr int OP_MMOV = 55;

    Tec2 tec2;
    vector<string> inst;
    bool db = false;
    bool scriptMode = false;
    bool requestedExit = false;
    int exitCode = 0;
    istream *input = &cin;
    int asmAddr = 0x800;
    int unasmAddr = 0x800;
    int dumpAddr = 0x800;
    int enterAddr = 0x800;

    bool init() {
        ifstream f("INST.ROM");
        if (!f.is_open()) {
            cerr << "初使化错误! 找不到文件INST.ROM!" << '\n';
            return false;
        }
        int num = 0;
        f >> num;
        string line;
        getline(f, line);
        inst.clear();
        inst.reserve(num);
        for (int i = 0; i < num; ++i) {
            getline(f, line);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            inst.push_back(line);
        }
        if (!tec2.ldmcReset()) {
            cerr << "初始化错误! 请确认所在目录下有文件 MCM_.ROM 和 MAPROM.ROM" << '\n';
            return false;
        }
        tec2.setReg(4, 0xffff);
        db = false;
        asmAddr = 0x800;
        unasmAddr = 0x800;
        dumpAddr = 0x800;
        enterAddr = 0x800;
        return true;
    }

    static void procStr(char *str) {
        int i, j, pre;
        int len = static_cast<int>(strlen(str));
        for (i = 0; i < len; i++) {
            if (str[i] == '\t') {
                str[i] = ' ';
            }
        }
        i = pre = 0;
        while (i < len) {
            while (i < len && str[i] == ' ') {
                i++;
            }
            for (j = i; j <= len; j++) {
                str[j - i + pre] = str[j];
            }
            len -= (i - pre);
            i = pre;
            while (i < len && str[i] != ' ') {
                i++;
            }
            i++;
            pre = i;
        }
        if (len > 0 && str[len - 1] == ' ') {
            str[--len] = 0;
        }
        if (str[1] == ' ') {
            for (i = 2; i <= len; i++) {
                str[i - 1] = str[i];
            }
            len--;
        }
        i = 0;
        while (i < len && str[i] != ',') {
            i++;
        }
        if (i < len) {
            if (i > 0 && str[i - 1] == ' ') {
                j = i--;
                while (j <= len) {
                    str[j - 1] = str[j];
                    j++;
                }
                len--;
            }
            if (i < len - 1 && str[i + 1] == ' ') {
                j = i + 2;
                while (j <= len) {
                    str[j - 1] = str[j];
                    j++;
                }
                len--;
            }
        }
        for (i = 0; i < len; i++) {
            str[i] = static_cast<char>(toupper(static_cast<unsigned char>(str[i])));
        }
    }

    static int hexValue(const char *str, char end = 0) {
        bool valid = true;
        int ret = 0;
        if (*str == end) {
            valid = false;
        }
        while (*str != end && valid) {
            if (isdigit(static_cast<unsigned char>(*str)) ||
                (*str >= 'A' && *str <= 'F') ||
                (*str >= 'a' && *str <= 'f')) {
                ret <<= 4;
                if (isdigit(static_cast<unsigned char>(*str))) {
                    ret += *str - '0';
                } else if (*str >= 'A' && *str <= 'F') {
                    ret += *str - 'A' + 10;
                } else {
                    ret += *str - 'a' + 10;
                }
                if (ret > 65535) {
                    valid = false;
                }
            } else {
                valid = false;
            }
            str++;
        }
        return valid ? ret : -1;
    }

    static int getReg(const char *str, char end = 0) {
        int n = -1;
        if (str[0] == 'R') {
            if (isdigit(static_cast<unsigned char>(str[1])) && str[2] == end) {
                n = str[1] - '0';
            } else if (str[1] == '1' && str[2] >= '0' && str[2] <= '5' && str[3] == end) {
                n = 10 + str[2] - '0';
            }
        } else if (str[1] && str[2] == end) {
            if (str[0] == 'S' && str[1] == 'P') {
                n = 4;
            } else if (str[0] == 'P' && str[1] == 'C') {
                n = 5;
            } else if (str[0] == 'I' && str[1] == 'P') {
                n = 6;
            }
        }
        return n;
    }

    int seekI(const char *ins) const {
        for (size_t i = 0; i < inst.size(); ++i) {
            if (inst[i] == ins) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    static void replaceToken(char *p, int size, int number) {
        if (p == nullptr) {
            return;
        }
        int len = static_cast<int>(strlen(p));
        for (int i = size; i <= len; i++) {
            p[i - size + 1] = p[i];
        }
        p[0] = static_cast<char>(number + '0');
    }

    static void replaceCnd(char *text) {
        replaceToken(strstr(text, "NC,"), 2, 0);
        replaceToken(strstr(text, "NZ,"), 2, 1);
        replaceToken(strstr(text, "NV,"), 2, 2);
        replaceToken(strstr(text, "NS,"), 2, 3);
        replaceToken(strstr(text, "C,"), 1, 4);
        replaceToken(strstr(text, "Z,"), 1, 5);
        replaceToken(strstr(text, "V,"), 1, 6);
        replaceToken(strstr(text, "S,"), 1, 7);
    }

    bool assembleInstruction(string text, vector<int> &words, bool allowCourseExtensions) const {
        char buffer[1000]{};
        strncpy(buffer, text.c_str(), sizeof(buffer) - 1);
        procStr(buffer);
        replaceCnd(buffer);

        words.clear();

        string normalized(buffer);
        if (allowCourseExtensions && normalized.rfind("MADD ", 0) == 0) {
            string body = normalized.substr(5);
            vector<string> parts;
            size_t start = 0;
            while (start <= body.size()) {
                size_t comma = body.find(',', start);
                parts.push_back(body.substr(start, comma == string::npos ? string::npos : comma - start));
                if (comma == string::npos) {
                    break;
                }
                start = comma + 1;
            }
            int dr = 9;
            int sr = 8;
            int addr1 = -1;
            int addr2 = -1;
            int addr3 = -1;
            if (parts.size() == 3) {
                addr1 = hexValue(parts[0].c_str());
                addr2 = hexValue(parts[1].c_str());
                addr3 = hexValue(parts[2].c_str());
            } else if (parts.size() == 5) {
                dr = getReg(parts[0].c_str());
                sr = getReg(parts[1].c_str());
                addr1 = hexValue(parts[2].c_str());
                addr2 = hexValue(parts[3].c_str());
                addr3 = hexValue(parts[4].c_str());
            } else {
                return false;
            }
            if (dr < 0 || sr < 0 || addr1 < 0 || addr2 < 0 || addr3 < 0) {
                return false;
            }
            words.push_back(OP_MADD << 10 | dr << 4 | sr);
            words.push_back(addr1);
            words.push_back(addr2);
            words.push_back(addr3);
            return true;
        }
        if (allowCourseExtensions && normalized.rfind("MMOV ", 0) == 0) {
            string body = normalized.substr(5);
            vector<string> parts;
            size_t start = 0;
            while (start <= body.size()) {
                size_t comma = body.find(',', start);
                parts.push_back(body.substr(start, comma == string::npos ? string::npos : comma - start));
                if (comma == string::npos) {
                    break;
                }
                start = comma + 1;
            }
            int dr = 9;
            int sr = 8;
            int addr1 = -1;
            int addr2 = -1;
            if (parts.size() == 2) {
                addr1 = hexValue(parts[0].c_str());
                addr2 = hexValue(parts[1].c_str());
            } else if (parts.size() == 4) {
                dr = getReg(parts[0].c_str());
                sr = getReg(parts[1].c_str());
                addr1 = hexValue(parts[2].c_str());
                addr2 = hexValue(parts[3].c_str());
            } else {
                return false;
            }
            if (dr < 0 || sr < 0 || addr1 < 0 || addr2 < 0) {
                return false;
            }
            words.push_back(OP_MMOV << 10 | dr << 4 | sr);
            words.push_back(addr1);
            words.push_back(addr2);
            return true;
        }
        if (allowCourseExtensions && normalized.rfind("JNE ", 0) == 0) {
            string body = normalized.substr(4);
            size_t c1 = body.find(',');
            size_t c2 = c1 == string::npos ? string::npos : body.find(',', c1 + 1);
            if (c1 == string::npos || c2 == string::npos || body.find(',', c2 + 1) != string::npos) {
                return false;
            }
            string drText = body.substr(0, c1);
            string srText = body.substr(c1 + 1, c2 - c1 - 1);
            string dispText = body.substr(c2 + 1);
            int dr = getReg(drText.c_str());
            int sr = getReg(srText.c_str());
            int disp = hexValue(dispText.c_str());
            if (dr < 0 || sr < 0 || disp < 0) {
                return false;
            }
            // Reuse the built-in NZ condition encoding:
            // opcode 54 fixes IR bit10 = 0, and bit9..8 = 01 selects Z then inverts it -> NZ.
            words.push_back(OP_JNE << 10 | 0x0100 | dr << 4 | sr);
            words.push_back(disp);
            return true;
        }

        int i, j, first, value1, value2, data;
        size_t len;
        int op;
        bool valid = true;
        int ir = 0;
        int ir2 = 0;
        bool hasIr2 = false;
        len = strlen(buffer);
        if (len == 0) {
            return false;
        }

        i = 0;
        while (i < static_cast<int>(len) && buffer[i] != ' ') {
            i++;
        }
        if (i < static_cast<int>(len)) {
            i++;
            first = i;
            if ((value1 = getReg(buffer + first)) >= 0) {
                if (strncmp("MUL", buffer, 3) == 0 || strncmp("DIV", buffer, 3) == 0 ||
                    strncmp("JP", buffer, 2) == 0 || strncmp("CALL", buffer, 4) == 0) {
                    strcpy(buffer + first, "SR");
                    if ((op = seekI(buffer)) < 0) {
                        valid = false;
                    }
                    ir = op << 10 | value1;
                } else {
                    strcpy(buffer + first, "DR");
                    if ((op = seekI(buffer)) < 0) {
                        valid = false;
                    }
                    ir = op << 10 | value1 << 4;
                }
            } else if ((value1 = getReg(buffer + first, ',')) >= 0) {
                i = first;
                while (buffer[i] != ',') {
                    i++;
                }
                i++;
                if ((value2 = getReg(buffer + i)) >= 0) {
                    strcpy(buffer + first, "DR,SR");
                    if ((op = seekI(buffer)) < 0) {
                        valid = false;
                    }
                    ir = op << 10 | value1 << 4 | value2;
                } else if ((data = hexValue(buffer + i)) >= 0) {
                    strcpy(buffer + first, "DR,DATA");
                    if ((op = seekI(buffer)) < 0) {
                        valid = false;
                    }
                    ir = op << 10 | value1 << 4;
                    ir2 = data;
                    hasIr2 = true;
                } else if ((data = hexValue(buffer + i, '[')) >= 0) {
                    j = i;
                    while (buffer[j] != '[') {
                        j++;
                    }
                    if ((value2 = getReg(buffer + j + 1, ']')) >= 0) {
                        strcpy(buffer + first, "DR,DATA[SR]");
                        if ((op = seekI(buffer)) < 0) {
                            valid = false;
                        }
                        ir = op << 10 | value1 << 4 | value2;
                        ir2 = data;
                        hasIr2 = true;
                    } else {
                        valid = false;
                    }
                } else if (buffer[i] == '[') {
                    if ((value2 = getReg(buffer + i + 1, ']')) >= 0) {
                        strcpy(buffer + first, "DR,[SR]");
                        if ((op = seekI(buffer)) < 0) {
                            valid = false;
                        }
                        ir = op << 10 | value1 << 4 | value2;
                    } else if ((data = hexValue(buffer + i + 1, ']')) >= 0) {
                        strcpy(buffer + first, "DR,[ADR]");
                        if ((op = seekI(buffer)) < 0) {
                            valid = false;
                        }
                        ir = op << 10 | value1 << 4;
                        ir2 = data;
                        hasIr2 = true;
                    } else {
                        valid = false;
                    }
                } else {
                    valid = false;
                }
            } else if ((data = hexValue(buffer + first, '[')) >= 0) {
                i = first;
                while (buffer[i] != '[') {
                    i++;
                }
                if ((value2 = getReg(buffer + i + 1, ']')) >= 0) {
                    j = i + 1;
                    while (buffer[j] != ']') {
                        j++;
                    }
                    if ((buffer[++j] == ',') && (value1 = getReg(buffer + j + 1)) >= 0) {
                        strcpy(buffer + first, "DATA[SR],DR");
                        if ((op = seekI(buffer)) < 0) {
                            valid = false;
                        }
                        ir = op << 10 | value1 << 4 | value2;
                        ir2 = data;
                        hasIr2 = true;
                    } else {
                        valid = false;
                    }
                } else {
                    valid = false;
                }
            } else if (buffer[first] == '[' && (value1 = getReg(buffer + first + 1, ']')) >= 0) {
                i = first;
                while (buffer[i] != ']') {
                    i++;
                }
                if (buffer[++i] == ',' && (value2 = getReg(buffer + i + 1)) >= 0) {
                    strcpy(buffer + first, "[DR],SR");
                    if ((op = seekI(buffer)) < 0) {
                        valid = false;
                    }
                    ir = op << 10 | value1 << 4 | value2;
                } else {
                    valid = false;
                }
            } else if (buffer[first] == '[' && (data = hexValue(buffer + first + 1, ']')) >= 0) {
                i = first;
                while (buffer[i] != ']') {
                    i++;
                }
                if (buffer[++i] == ',' && (value2 = getReg(buffer + i + 1)) >= 0) {
                    strcpy(buffer + first, "[ADR],SR");
                    if ((op = seekI(buffer)) < 0) {
                        valid = false;
                    }
                    ir = op << 10 | value2;
                    ir2 = data;
                    hasIr2 = true;
                } else {
                    valid = false;
                }
            } else if ((data = hexValue(buffer + first)) >= 0) {
                if (strncmp("JP", buffer, 2) != 0 && data >= 256) {
                    valid = false;
                } else if (strncmp("IN", buffer, 2) == 0 || strncmp("OUT", buffer, 3) == 0) {
                    strcpy(buffer + first, "PORT");
                    if ((op = seekI(buffer)) < 0) {
                        valid = false;
                    }
                    ir = op << 10 | data;
                } else if (strncmp("JP", buffer, 2) == 0) {
                    strcpy(buffer + first, "ADR");
                    if ((op = seekI(buffer)) < 0) {
                        valid = false;
                    }
                    ir = op << 10;
                    ir2 = data;
                    hasIr2 = true;
                } else {
                    strcpy(buffer + first, "ADR");
                    if ((op = seekI(buffer)) < 0) {
                        valid = false;
                    }
                    ir = op << 10 | data;
                }
            } else if ((data = hexValue(buffer + first, ',')) >= 0) {
                if (data >= 8) {
                    valid = false;
                } else if ((value1 = getReg(buffer + first + 2)) >= 0) {
                    strcpy(buffer + first, "CND,SR");
                    if ((op = seekI(buffer)) < 0) {
                        valid = false;
                    }
                    ir = op << 10 | data << 8 | value1;
                } else if ((value1 = hexValue(buffer + first + 2)) >= 0) {
                    strcpy(buffer + first, "CND,ADR");
                    if ((op = seekI(buffer)) < 0) {
                        valid = false;
                    }
                    ir = op << 10 | data << 8;
                    ir2 = value1;
                    hasIr2 = true;
                } else {
                    valid = false;
                }
            } else {
                valid = false;
            }
        } else if ((op = seekI(buffer)) >= 0) {
            ir = op << 10;
        } else {
            valid = false;
        }

        if (!valid) {
            return false;
        }
        words.push_back(ir);
        if (hasIr2) {
            words.push_back(ir2);
        }
        return true;
    }

    string formatRegName(int r) const {
        if (r == 4) {
            return "SP";
        }
        if (r == 5) {
            return "PC";
        }
        if (r == 6) {
            return "IP";
        }
        return "R_" + to_string(r);
    }

    string disassembleAt(int address, int &size, bool allowCourseExtensions = false) const {
        ostringstream out;
        int op = getBits(tec2.MEM[address], 15, 10);
        size = 1;
        out << uppercase << hex << setfill('0');
        out << setw(4) << (address & 0xffff) << ": ";
        out << setw(4) << (tec2.MEM[address] & 0xffff) << ' ';
        if (allowCourseExtensions && op == OP_MADD) {
            size = 4;
            out << setw(4) << (tec2.MEM[(address + 1) & 0xffff] & 0xffff) << ' ';
            out << setw(4) << (tec2.MEM[(address + 2) & 0xffff] & 0xffff) << ' ';
            out << setw(4) << (tec2.MEM[(address + 3) & 0xffff] & 0xffff) << "  ";
            if (getBits(tec2.MEM[address], 7, 0) == 0) {
                out << "MADD "
                    << setw(4) << (tec2.MEM[(address + 1) & 0xffff] & 0xffff) << ", "
                    << setw(4) << (tec2.MEM[(address + 2) & 0xffff] & 0xffff) << ", "
                    << setw(4) << (tec2.MEM[(address + 3) & 0xffff] & 0xffff);
            } else {
                out << "MADD "
                    << formatRegName(getBits(tec2.MEM[address], 7, 4)) << ", "
                    << formatRegName(getBits(tec2.MEM[address], 3, 0)) << ", "
                    << setw(4) << (tec2.MEM[(address + 1) & 0xffff] & 0xffff) << ", "
                    << setw(4) << (tec2.MEM[(address + 2) & 0xffff] & 0xffff) << ", "
                    << setw(4) << (tec2.MEM[(address + 3) & 0xffff] & 0xffff);
            }
            return out.str();
        }
        if (allowCourseExtensions && op == OP_MMOV) {
            size = 3;
            out << setw(4) << (tec2.MEM[(address + 1) & 0xffff] & 0xffff) << ' ';
            out << setw(4) << (tec2.MEM[(address + 2) & 0xffff] & 0xffff) << "       ";
            if (getBits(tec2.MEM[address], 7, 0) == 0) {
                out << "MMOV "
                    << setw(4) << (tec2.MEM[(address + 1) & 0xffff] & 0xffff) << ", "
                    << setw(4) << (tec2.MEM[(address + 2) & 0xffff] & 0xffff);
            } else {
                out << "MMOV "
                    << formatRegName(getBits(tec2.MEM[address], 7, 4)) << ", "
                    << formatRegName(getBits(tec2.MEM[address], 3, 0)) << ", "
                    << setw(4) << (tec2.MEM[(address + 1) & 0xffff] & 0xffff) << ", "
                    << setw(4) << (tec2.MEM[(address + 2) & 0xffff] & 0xffff);
            }
            return out.str();
        }
        if (allowCourseExtensions && op == OP_JNE) {
            size = 2;
            out << setw(4) << (tec2.MEM[(address + 1) & 0xffff] & 0xffff) << "  ";
            out << "JNE "
                << formatRegName(getBits(tec2.MEM[address], 7, 4)) << ", "
                << formatRegName(getBits(tec2.MEM[address], 3, 0)) << ", "
                << setw(4) << (tec2.MEM[(address + 1) & 0xffff] & 0xffff);
            return out.str();
        }
        if (op < static_cast<int>(inst.size())) {
            if ((inst[op].find("ADR") != string::npos && inst[op].rfind("JR", 0) != 0) ||
                inst[op].find("DATA") != string::npos) {
                out << setw(4) << (tec2.MEM[(address + 1) & 0xffff] & 0xffff) << "  ";
                size = 2;
            } else {
                out << "      ";
            }
            const string &templ = inst[op];
            size_t j = 0;
            while (j < templ.size() && templ[j] != ' ') {
                out << templ[j++];
            }
            while (j < templ.size()) {
                if (templ.compare(j, 2, "DR") == 0) {
                    out << formatRegName(getBits(tec2.MEM[address], 7, 4));
                    j += 2;
                } else if (templ.compare(j, 2, "SR") == 0) {
                    out << formatRegName(getBits(tec2.MEM[address], 3, 0));
                    j += 2;
                } else if (templ.compare(j, 3, "ADR") == 0) {
                    if (templ.rfind("JR", 0) == 0) {
                        out << setw(2) << getBits(tec2.MEM[address], 7, 0);
                    } else {
                        out << setw(4) << (tec2.MEM[(address + 1) & 0xffff] & 0xffff);
                    }
                    j += 3;
                } else if (templ.compare(j, 4, "DATA") == 0) {
                    out << setw(4) << (tec2.MEM[(address + 1) & 0xffff] & 0xffff);
                    j += 4;
                } else if (templ.compare(j, 4, "PORT") == 0) {
                    out << setw(2) << getBits(tec2.MEM[address], 7, 0);
                    j += 4;
                } else if (templ.compare(j, 3, "CND") == 0) {
                    if (!getBit(tec2.MEM[address], 10)) {
                        out << 'N';
                    }
                    switch (getBits(tec2.MEM[address], 9, 8)) {
                        case 0: out << 'C'; break;
                        case 1: out << 'Z'; break;
                        case 2: out << 'V'; break;
                        case 3: out << 'S'; break;
                        default: break;
                    }
                    j += 3;
                } else {
                    if (templ[j] == ',') {
                        out << ",\t";
                    } else if (templ[j] == ' ') {
                        out << '\t';
                    } else {
                        out << templ[j];
                    }
                    j++;
                }
            }
        } else {
            out << "      DW     " << setw(4) << (tec2.MEM[address] & 0xffff);
        }
        return out.str();
    }

    void printRegs(bool showInst = true) {
        int r[16]{};
        tec2.getReg(r);
        cout << uppercase << hex << setfill('0');
        for (int i = 0; i < 4; ++i) {
            cout << "R_" << dec << i << '=' << hex << setw(4) << (r[i] & 0xffff) << "  ";
        }
        cout << "SP=" << setw(4) << (r[4] & 0xffff) << "  ";
        cout << "PC=" << setw(4) << (r[5] & 0xffff) << "  ";
        cout << "IP=" << setw(4) << (r[6] & 0xffff) << "  ";
        cout << "R7=" << setw(4) << (r[7] & 0xffff) << "  ";
        cout << "R8=" << setw(4) << (r[8] & 0xffff) << '\n';
        for (int i = 9; i < 16; ++i) {
            cout << "R_" << dec << i << '=' << hex << setw(4) << (r[i] & 0xffff) << ' ';
        }
        int flags = tec2.getFlags();
        cout << "  F_=";
        for (int i = 7; i >= 0; --i) {
            cout << getBit(flags, i);
        }
        cout << '\n';
        if (showInst) {
            int next = 0;
            cout << disassembleAt(r[5] & 0xffff, next, scriptMode) << '\n';
        }
    }

    void dumpMem(int address, int count) const {
        cout << uppercase << hex << setfill('0');
        auto printable = [](char c) {
            return c >= 0x21 && c <= 0x7e;
        };
        for (int i = 0; i < count; ++i) {
            int rowAddr = (address + i * 8) & 0xffff;
            cout << setw(4) << rowAddr << "    ";
            for (int j = 0; j < 8; ++j) {
                cout << setw(4) << (tec2.MEM[(rowAddr + j) & 0xffff] & 0xffff) << "  ";
            }
            for (int j = 0; j < 8; ++j) {
                char c = static_cast<char>((tec2.MEM[(rowAddr + j) & 0xffff] & 0xff00) >> 8);
                if (!printable(c)) {
                    c = '.';
                }
                cout << c;
                c = static_cast<char>(tec2.MEM[(rowAddr + j) & 0xffff] & 0x00ff);
                if (!printable(c)) {
                    c = '.';
                }
                cout << c;
            }
            cout << '\n';
        }
    }

    void viewStatus() const {
        int B0 = tec2.PLR[0].Y;
        int B1 = tec2.PLR[1].Y;
        int B2 = tec2.PLR[2].Y;
        int B3 = tec2.PLR[3].Y;
        int MRA = getBits(B3, 7, 0) << 2 | getBits(B2, 15, 14);
        int A = getBits(B1, 3, 0);
        int B = getBits(B0, 15, 12);
        cout << uppercase << hex << setfill('0');
        cout << "Next: " << setw(3) << tec2.MCTL.Y << ' ';
        for (int i = 3; i >= 0; --i) {
            cout << "  " << setw(4) << tec2.PLR[i].Y;
        }
        cout << '\n';
        cout << "Analyse: ";
        cout << "AR_=" << setw(4) << tec2.getAr()
             << "  MEM=" << setw(4) << tec2.MEM[tec2.getAr()]
             << "  MRA_=" << setw(3) << MRA
             << "  A_=" << setw(2) << A
             << "  B_=" << setw(2) << B << '\n';
        cout << "C=" << getBit(tec2.getFlags(), 7)
             << "  Z=" << getBit(tec2.getFlags(), 6)
             << "  V=" << getBit(tec2.getFlags(), 5)
             << "  S=" << getBit(tec2.getFlags(), 4) << '\n';
    }

    void microStep() {
        if (!db) {
            db = true;
            tec2.setStatus(5, 0, 0x29, 0x0300, 0x90f0, 0);
            while (tec2.MCTL.Y != 0x1a) {
                tec2.run(1);
            }
        }
        tec2.run(1);
        if (tec2.MCTL.Y == 0xa4) {
            db = false;
            while (tec2.MCTL.Y != 0x1a) {
                tec2.run(1);
            }
            tec2.run(1);
        }
        viewStatus();
        printRegs(false);
    }

    static vector<string> splitArgs(const string &text) {
        istringstream in(text);
        vector<string> out;
        string part;
        while (in >> part) {
            out.push_back(part);
        }
        return out;
    }

    static string trimCopy(const string &text) {
        size_t first = text.find_first_not_of(" \t");
        if (first == string::npos) {
            return "";
        }
        size_t last = text.find_last_not_of(" \t");
        return text.substr(first, last - first + 1);
    }

    static string upperCopy(string text) {
        for (char &ch : text) {
            ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
        }
        return text;
    }

    static string filterInteractiveHex(const string &text, size_t len = 4) {
        string out;
        for (char ch : text) {
            unsigned char c = static_cast<unsigned char>(ch);
            if (out.size() < len && isxdigit(c)) {
                out.push_back(static_cast<char>(toupper(c)));
            }
        }
        return out;
    }

    static char lastChoiceChar(const string &text, const string &choices, char fallback, ostream *echo = nullptr) {
        char selected = fallback;
        for (char ch : text) {
            char upper = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
            if (choices.find(upper) != string::npos) {
                selected = upper;
                if (echo) {
                    *echo << upper << '\b';
                }
            }
        }
        return selected;
    }

    bool readConsoleLine(string &line) const {
        return static_cast<bool>(getline(*input, line));
    }

    bool readHexInput(string &value, size_t len = 4) const {
        value.clear();
#ifdef _WIN32
        if (input == &cin && _isatty(_fileno(stdin))) {
            while (true) {
                int ch = _getch();
                if (ch == '\r' || ch == '\n') {
                    cout << '\n';
                    return true;
                }
                if (ch == '\b' || ch == 127) {
                    if (!value.empty()) {
                        value.pop_back();
                        cout << "\b \b";
                    }
                    continue;
                }
                unsigned char c = static_cast<unsigned char>(ch);
                if (value.size() < len && isxdigit(c)) {
                    cout << static_cast<char>(ch);
                    value.push_back(static_cast<char>(toupper(c)));
                }
            }
        }
#endif
        string line;
        if (!readConsoleLine(line)) {
            cout << '\n';
            return false;
        }
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        for (char ch : line) {
            if (ch == '\b' || ch == 127) {
                if (!value.empty()) {
                    value.pop_back();
                    cout << "\b \b";
                }
                continue;
            }
            unsigned char c = static_cast<unsigned char>(ch);
            if (value.size() < len && isxdigit(c)) {
                cout << ch;
                value.push_back(static_cast<char>(toupper(c)));
            }
        }
        cout << '\n';
        return true;
    }

    bool printOrEditRegsAfterStep(const string &args) {
        if (args.empty()) {
            printRegs();
            return true;
        }
        int reg = getReg(args.c_str());
        if (reg < 0) {
            cout << "Error" << '\n';
            return true;
        }
        int regs[16]{};
        tec2.getReg(regs);
        cout << uppercase << hex << setfill('0') << setw(4) << (regs[reg] & 0xffff) << ":-";
        string valueLine;
        if (!readHexInput(valueLine)) {
            return false;
        }
        if (!valueLine.empty()) {
            int value = 0;
            if (!parseHexWord(valueLine, value)) {
                cout << "Error" << '\n';
                return true;
            }
            tec2.setReg(reg, value);
        }
        return true;
    }

    bool readInteractiveCommand(string &line, bool &openF10) const {
        openF10 = false;
#ifdef _WIN32
        if (!_isatty(_fileno(stdin))) {
            string raw;
            if (!readConsoleLine(raw)) {
                return false;
            }
            line.clear();
            for (unsigned char ch : raw) {
                if (ch == '\b' || ch == 127) {
                    if (!line.empty()) {
                        line.pop_back();
                        cout << "\b \b";
                    }
                    continue;
                }
                if (isgraph(ch) || ch == ' ') {
                    line.push_back(static_cast<char>(ch));
                    cout << static_cast<char>(ch);
                }
            }
            if (!line.empty()) {
                cout << '\n';
            }
            return true;
        }
        line.clear();
        while (true) {
            int ch = _getch();
            if (ch == '\r' || ch == '\n') {
                cout << '\n';
                return true;
            }
            if (ch == 0 || ch == 0xE0) {
                int ext = _getch();
                if (ext == 68) {
                    openF10 = true;
                    cout << '\n';
                    return true;
                }
                continue;
            }
            if (ch == '\b' || ch == 127) {
                if (!line.empty()) {
                    line.pop_back();
                    cout << "\b \b";
                }
                continue;
            }
            if (isgraph(ch) || ch == ' ') {
                line.push_back(static_cast<char>(ch));
                cout << static_cast<char>(ch);
            }
        }
#else
        return readConsoleLine(line);
#endif
    }

    bool runF10Menu() {
        cout << "\n\n\n\n\n";
        cout << "\t\t0---Return to TEC-2 CRT Monitor\n";
        cout << "\t\t1---Send a file to TEC-2\n";
        cout << "\t\t2---Receive a file from TEC-2\n";
        cout << "\t\t3---Return to PC(MS)-DOS\n\n";
        cout << "\t\tEnter your choice:[0]\b\b";

        string line;
        if (!readConsoleLine(line)) {
            cout << '\n';
            return false;
        }
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        char choice = lastChoiceChar(line, "0123", '0', &cout);
        cout << '\n';

        switch (choice) {
            case '0':
                return true;
            case '1': {
                cout << "\n\t\tFile name for read:";
                string name;
                if (!readConsoleLine(name)) {
                    cout << "\n00000 bytes\n\nFile not found\n\n";
                    return false;
                }
                if (!name.empty() && name.back() == '\r') {
                    name.pop_back();
                }
                cout << name << '\n';
                array<char, 512> buffer{};
                strncpy(buffer.data(), name.c_str(), buffer.size() - 1);
                int bytes = tec2.readFile(buffer.data());
                if (bytes < 0) {
                    cout << "\n00000 bytes\n\nFile not found\n\n";
                } else {
                    cout << '\n' << uppercase << dec << setw(5) << setfill('0') << bytes << " bytes\n\n";
                }
                return true;
            }
            case '2': {
                cout << "\n\t\tFile name for write:";
                string name;
                if (!readConsoleLine(name)) {
                    cout << "\n\nFile not found\n\n";
                    return false;
                }
                if (!name.empty() && name.back() == '\r') {
                    name.pop_back();
                }
                cout << name << '\n';
                cout << "\t\tBegin from: ";
                string fromText;
                if (!readHexInput(fromText)) {
                    cout << "\n\nFile not found\n\n";
                    return false;
                }
                cout << "\t\tData length: ";
                string lenText;
                if (!readHexInput(lenText)) {
                    cout << "\n\nFile not found\n\n";
                    return false;
                }
                int from = hexValue(fromText.c_str());
                int len = hexValue(lenText.c_str());
                array<char, 512> buffer{};
                strncpy(buffer.data(), name.c_str(), buffer.size() - 1);
                int bytes = tec2.writeFile(buffer.data(), from, len);
                if (bytes < 0) {
                    cout << "\n\nFile not found\n\n";
                } else {
                    cout << '\n' << uppercase << dec << setw(5) << setfill('0') << bytes << " bytes\n\n";
                }
                return true;
            }
            case '3':
                requestedExit = true;
                exitCode = 0;
                return false;
            default:
                return true;
        }
    }

    void runInteractiveAssemble() {
        cout << uppercase << hex << setfill('0');
        while (true) {
            cout << setw(4) << asmAddr << ": ";
            string line;
            if (!readConsoleLine(line)) {
                break;
            }
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            cout << line << '\n';
            line = trimCopy(line);
            if (line.empty()) {
                break;
            }
            vector<int> words;
            if (!assembleInstruction(line, words, scriptMode)) {
                cout << "\tError" << '\n';
                continue;
            }
            for (size_t i = 0; i < words.size(); ++i) {
                tec2.MEM[(asmAddr + static_cast<int>(i)) & 0xffff] = words[i];
            }
            asmAddr += static_cast<int>(words.size());
        }
    }

    void runInteractiveEdit() {
        cout << uppercase << hex << setfill('0');
        int addr = enterAddr;
        while (true) {
            if ((addr - enterAddr) % 5 == 0) {
                if (addr != enterAddr) {
                    cout << '\n';
                }
                cout << setw(4) << addr << '\t';
            }
            cout << setw(4) << (tec2.MEM[addr & 0xffff] & 0xffff) << ':';
            string line;
            if (!readConsoleLine(line)) {
                break;
            }
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            if (line.empty()) {
                enterAddr = (addr + 1) & 0xffff;
                cout << '\n';
                break;
            }

            string valueText;
            auto commitValue = [&]() {
                if (!valueText.empty()) {
                    int value = 0;
                    if (parseHexWord(valueText, value)) {
                        tec2.MEM[addr & 0xffff] = value;
                    }
                    valueText.clear();
                }
            };
            for (char ch : line) {
                if (ch == ' ') {
                    size_t valueLen = valueText.size();
                    commitValue();
                    for (size_t pad = valueLen; pad < 4; ++pad) {
                        cout << ' ';
                    }
                    cout << "  ";
                    addr++;
                    if ((addr - enterAddr) % 5 == 0) {
                        cout << '\n';
                        cout << setw(4) << addr << '\t';
                    }
                    cout << setw(4) << (tec2.MEM[addr & 0xffff] & 0xffff) << ':';
                } else {
                    if (ch == '\b' || ch == 127) {
                        if (!valueText.empty()) {
                            valueText.pop_back();
                            cout << "\b \b";
                        }
                        continue;
                    }
                    unsigned char c = static_cast<unsigned char>(ch);
                    if (valueText.size() < 4 && isxdigit(c)) {
                        cout << ch;
                        valueText.push_back(static_cast<char>(toupper(c)));
                    }
                }
            }
            commitValue();
            enterAddr = (addr + 1) & 0xffff;
            cout << '\n';
            break;
        }
    }

    static bool parseHexWord(const string &text, int &value) {
        value = hexValue(text.c_str());
        return value >= 0;
    }

    bool handleLine(const string &rawLine, bool /*echoPrompt*/) {
        string line = rawLine;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!scriptMode) {
            string filtered;
            for (unsigned char ch : line) {
                if (isgraph(ch) || ch == ' ') {
                    filtered.push_back(static_cast<char>(ch));
                }
            }
            line = filtered;
        }
        if (line.empty()) {
            return true;
        }
        if (scriptMode && (line[0] == '#' || line[0] == ';')) {
            return true;
        }

        line = trimCopy(line);
        if (line.empty()) {
            return true;
        }
        if (scriptMode && (line[0] == '#' || line[0] == ';')) {
            return true;
        }
        string dispatchLine = scriptMode ? line : upperCopy(line);
        char buffer[1000]{};
        strncpy(buffer, dispatchLine.c_str(), sizeof(buffer) - 1);
        procStr(buffer);

        string cmd;
        string args;
        string normalized(buffer);
        string upper = upperCopy(normalized);
        size_t split = upper.find_first_of(" \t");
        auto isLongCommand = [&](const string &token) {
            if (!scriptMode) {
                return false;
            }
            return token == "HELP" || token == "LOAD" || token == "SAVE" ||
                   token == "RESET" || token == "QUIT" || token == "EXIT" ||
                   token == "SAVEMCR";
        };
        string firstToken = upperCopy(split == string::npos ? upper : upper.substr(0, split));
        if (scriptMode && upper.rfind("F10", 0) == 0 &&
            (upper.size() == 3 || isspace(static_cast<unsigned char>(upper[3])))) {
            cmd = "F10";
            args = trimCopy(normalized.substr(3));
        } else if (upper.rfind("TT", 0) == 0) {
            cmd = "TT";
            args = trimCopy(upper.substr(2));
        } else if (upper.size() > 1 && !isLongCommand(firstToken)) {
            cmd = upper.substr(0, 1);
            args = trimCopy(upper.substr(1));
        } else {
            cmd = firstToken;
            args = split == string::npos ? "" : trimCopy(normalized.substr(split + 1));
        }

        if (cmd == "Q" || (scriptMode && (cmd == "QUIT" || cmd == "EXIT"))) {
            if (!args.empty()) {
                cout << "Unknown command! Type ? for help." << '\n';
                return true;
            }
            requestedExit = true;
            exitCode = scriptMode ? 0 : 1;
            return false;
        }
        if (cmd == "?" || (scriptMode && cmd == "HELP")) {
            cout << "Assemble\tA_ [adr]\n"
                 << "Unassemble\tU [adr]\n"
                 << "Go\t\tG [adr]\n"
                 << "Trace\t\tT [adr]\n"
                 << "Proceed\t\tP [adr]\n"
                 << "Reg\tR_ [reg]\n"
                 << "Dump\t\tD [adr]\n"
                 << "Enter\t\tE [adr]\n"
                 << "Hex\t\tH value1 value2\n"
                 << "saveMcm\t\tS\n"
                 << "MStep\t\tTT\n"
                 << "Clear\t\tC\n"
                 << "Quit\t\tQ\n";
            return true;
        }
        if (cmd == "RESET") {
            if (!scriptMode) {
                cout << "Unknown command! Type ? for help." << '\n';
                return true;
            }
            tec2 = Tec2();
            if (!init()) {
                return false;
            }
            cout << "reset ok" << '\n';
            return true;
        }
        if (cmd == "F10") {
            return runF10Menu();
        }
        if (cmd == "C") {
            if (!scriptMode) {
                cout << "Unknown command! Type ? for help." << '\n';
                return true;
            }
            if (!args.empty()) {
                cout << "Unknown command! Type ? for help." << '\n';
                return true;
            }
            cout << string(40, '\n');
            return true;
        }
        if (cmd == "R") {
            if (args.empty()) {
                printRegs();
                return true;
            }
            vector<string> parts = splitArgs(args);
            if (parts.empty() || parts.size() > 2) {
                cout << "Error" << '\n';
                return true;
            }
            int reg = getReg(upperCopy(parts[0]).c_str());
            if (reg < 0) {
                cout << "Error" << '\n';
                return true;
            }
            if (parts.size() == 1) {
                if (scriptMode) {
                    printRegs();
                    return true;
                }
                int regs[16]{};
                tec2.getReg(regs);
                cout << uppercase << hex << setfill('0');
                cout << setw(4) << (regs[reg] & 0xffff) << ":-";
                string valueLine;
                if (!readHexInput(valueLine)) {
                    return false;
                }
                if (!valueLine.empty()) {
                    int value = 0;
                    if (!parseHexWord(valueLine, value)) {
                        cout << "Error" << '\n';
                        return true;
                    }
                    tec2.setReg(reg, value);
                }
                return true;
            }
            if (!scriptMode) {
                cout << "Error" << '\n';
                return true;
            }
            int value = 0;
            if (!parseHexWord(parts[1], value)) {
                cout << "Error" << '\n';
                return true;
            }
            tec2.setReg(reg, value);
            printRegs();
            return true;
        }
        if (cmd == "D") {
            vector<string> parts = splitArgs(args);
            int count = 15;
            if (!scriptMode && parts.size() > 1) {
                cout << "Error!" << '\n';
                return true;
            }
            if (!parts.empty()) {
                int addr = 0;
                if (!parseHexWord(parts[0], addr)) {
                    cout << "Error!" << '\n';
                    return true;
                }
                dumpAddr = addr;
            }
            if (parts.size() > 1 && !parseHexWord(parts[1], count)) {
                cout << "Error!" << '\n';
                return true;
            }
            dumpMem(dumpAddr, count);
            dumpAddr = (dumpAddr + count * 8) & 0xffff;
            return true;
        }
        if (cmd == "M") {
            if (!scriptMode) {
                cout << "Unknown command! Type ? for help." << '\n';
                return true;
            }
            vector<string> parts = splitArgs(args);
            if (parts.size() < 2) {
                cerr << "usage: M <addr> <word...>" << '\n';
                return true;
            }
            int addr = 0;
            if (!parseHexWord(parts[0], addr)) {
                cerr << "invalid address" << '\n';
                return true;
            }
            for (size_t i = 1; i < parts.size(); ++i) {
                int value = 0;
                if (!parseHexWord(parts[i], value)) {
                    cerr << "invalid word: " << parts[i] << '\n';
                    return true;
                }
                tec2.MEM[(addr++) & 0xffff] = value;
            }
            return true;
        }
        if (cmd == "A") {
            if (!scriptMode || args.empty() || (args.find(' ') == string::npos && args.find('\t') == string::npos && args.find(',') == string::npos)) {
                if (!args.empty()) {
                    int addr = 0;
                    if (!parseHexWord(args, addr)) {
                        cout << "Error" << '\n';
                        return true;
                    }
                    asmAddr = addr & 0xffff;
                }
                if (!scriptMode) {
                    runInteractiveAssemble();
                    return true;
                }
            }
            size_t pos = args.find(' ');
            if (pos == string::npos) {
                cout << "Error" << '\n';
                return true;
            }
            int addr = 0;
            if (!parseHexWord(args.substr(0, pos), addr)) {
                cout << "Error" << '\n';
                return true;
            }
            string instText = args.substr(pos + 1);
            vector<int> words;
            if (!assembleInstruction(instText, words, scriptMode)) {
                cout << "Error" << '\n';
                return true;
            }
            for (size_t i = 0; i < words.size(); ++i) {
                tec2.MEM[(addr + static_cast<int>(i)) & 0xffff] = words[i];
            }
            int size = 0;
            cout << disassembleAt(addr & 0xffff, size, scriptMode) << '\n';
            asmAddr = (addr + static_cast<int>(words.size())) & 0xffff;
            return true;
        }
        if (cmd == "U") {
            vector<string> parts = splitArgs(args);
            int count = 16;
            if (!scriptMode && parts.size() > 1) {
                cout << "Error" << '\n';
                return true;
            }
            if (!parts.empty()) {
                int addr = 0;
                if (!parseHexWord(parts[0], addr)) {
                    cout << "Error" << '\n';
                    return true;
                }
                unasmAddr = addr;
            }
            if (parts.size() > 1 && !parseHexWord(parts[1], count)) {
                cout << "Error" << '\n';
                return true;
            }
            for (int i = 0; i < count; ++i) {
                int size = 0;
                cout << disassembleAt(unasmAddr & 0xffff, size, scriptMode) << '\n';
                unasmAddr = (unasmAddr + size) & 0xffff;
            }
            return true;
        }
        if (cmd == "B") {
            if (args.empty()) {
                tec2.bp = -1;
                return true;
            }
            int addr = 0;
            if (!parseHexWord(args, addr)) {
                cout << "Error" << '\n';
                return true;
            }
            tec2.bp = addr & 0xffff;
            return true;
        }
        if (cmd == "G") {
            int addr = -1;
            if (!args.empty() && !parseHexWord(args, addr)) {
                cout << "Error" << '\n';
                return true;
            }
            if (args.empty()) {
                addr = -1;
            } else {
                tec2.setReg(5, addr);
            }
            if (!tec2.cont(addr)) {
                cout << "Dead cycle!" << '\n';
            }
            return true;
        }
        if (cmd == "T") {
            tec2.step();
            return printOrEditRegsAfterStep(args);
        }
        if (cmd == "P") {
            tec2.step2();
            return printOrEditRegsAfterStep(args);
        }
        if (cmd == "TT") {
            if (!args.empty()) {
                cout << "Error" << '\n';
                return true;
            }
            microStep();
            return true;
        }
        if (cmd == "V") {
            viewStatus();
            return true;
        }
        if (cmd == "E") {
            if (!args.empty()) {
                int addr = 0;
                if (!parseHexWord(args, addr)) {
                    cout << "Error" << '\n';
                    return true;
                }
                enterAddr = addr & 0xffff;
            }
            if (!scriptMode) {
                runInteractiveEdit();
                return true;
            }
            return true;
        }
        if (cmd == "H") {
            vector<string> parts = splitArgs(args);
            if (parts.size() != 2) {
                cout << "Error" << '\n';
                return true;
            }
            int value1 = 0;
            int value2 = 0;
            if (!parseHexWord(parts[0], value1) || !parseHexWord(parts[1], value2)) {
                cout << "Error" << '\n';
                return true;
            }
            int sum = value1 + value2;
            if (sum > 65535) {
                sum -= 65536;
            }
            int diff = value1 - value2;
            if (diff < 0) {
                diff += 65536;
            }
            cout << uppercase << hex << setfill('0');
            cout << setw(4) << sum << "  " << setw(4) << diff << '\n';
            return true;
        }
        if (cmd == "S") {
            if (!args.empty()) {
                cout << "Unknown command! Type ? for help." << '\n';
                return true;
            }
            bool yes = false;
            cout << "Are you sure to save the MCM_? [N]\b\b";
            string answer;
            if (!readConsoleLine(answer)) {
                cout << '\n';
                return false;
            }
            yes = lastChoiceChar(answer, "YN", 'N', &cout) == 'Y';
            cout << '\n';
            if (yes) {
                tec2.saveMcm();
            }
            return true;
        }
        if (cmd == "LOAD") {
            if (!scriptMode) {
                cout << "Unknown command! Type ? for help." << '\n';
                return true;
            }
            if (args.empty()) {
                cerr << "usage: LOAD <file>" << '\n';
                return true;
            }
            array<char, 512> name{};
            strncpy(name.data(), args.c_str(), name.size() - 1);
            int bytes = tec2.readFile(name.data());
            if (bytes < 0) {
                cerr << "load failed" << '\n';
            } else {
                cout << "loaded " << dec << bytes << " bytes" << '\n';
            }
            return true;
        }
        if (cmd == "SAVE") {
            if (!scriptMode) {
                cout << "Unknown command! Type ? for help." << '\n';
                return true;
            }
            vector<string> parts = splitArgs(args);
            if (parts.size() != 3) {
                cerr << "usage: SAVE <file> <addr> <count>" << '\n';
                return true;
            }
            int addr = 0;
            int count = 0;
            if (!parseHexWord(parts[1], addr) || !parseHexWord(parts[2], count)) {
                cerr << "invalid address/count" << '\n';
                return true;
            }
            array<char, 512> name{};
            strncpy(name.data(), parts[0].c_str(), name.size() - 1);
            int bytes = tec2.writeFile(name.data(), addr, count);
            if (bytes < 0) {
                cerr << "save failed" << '\n';
            } else {
                cout << "saved " << dec << bytes << " bytes" << '\n';
            }
            return true;
        }
        if (cmd == "SAVEMCR") {
            if (!scriptMode) {
                cout << "Unknown command! Type ? for help." << '\n';
                return true;
            }
            tec2.saveMcm();
            cout << "saved MCR.ROM" << '\n';
            return true;
        }

        cout << "Unknown command! Type ? for help." << '\n';
        return true;
    }
};

} // namespace

int main(int argc, char **argv) {
    App app;
    bool serverMode = (argc > 1 && std::string(argv[1]) == "--server");
    app.scriptMode = argc > 1;
    if (!app.init()) {
        return 2;
    }

    // Persistent line-protocol server for tooling (e.g. the MCP server).
    // Reads one command per stdin line, keeps full machine state across
    // lines, and emits a fixed sentinel after each command so the caller
    // knows the output is complete. Script-mode extensions (MADD/MMOV/JNE
    // assembly, M/RESET/LOAD/SAVE, R reg value, D/U addr count) are enabled.
    if (serverMode) {
        app.input = &cin;
        cout << "__TEC2_READY__\n";
        cout.flush();
        string line;
        while (getline(cin, line)) {
            bool keepGoing = app.handleLine(line, false);
            cout << "__TEC2_DONE__\n";
            cout.flush();
            if (!keepGoing) {
                break;
            }
        }
        return app.exitCode;
    }

    if (argc > 1) {
        ifstream script(argv[1]);
        if (!script.is_open()) {
            cerr << "cannot open script: " << argv[1] << '\n';
            return 1;
        }
        app.input = &script;
        string line;
        while (getline(script, line)) {
            if (!app.handleLine(line, false)) {
                break;
            }
        }
        return 0;
    }

    cout << "TEC-2 CRT MONITOR\nfor macOS / Linux, June.2022\n\n";
    string line;
    while (true) {
        cout << "> ";
        bool openF10 = false;
        if (!app.readInteractiveCommand(line, openF10)) {
            break;
        }
        if (openF10) {
            if (!app.runF10Menu()) {
                break;
            }
            continue;
        }
        if (!app.handleLine(line, true)) {
            break;
        }
    }
    return app.requestedExit ? app.exitCode : 0;
}
