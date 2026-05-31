#include "compiler/PELinker.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <algorithm>
#include <vector>

namespace {

// --- Windows/COFF structures ---
#pragma pack(push, 1)
struct CoffFileHeader {
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
};

struct CoffSectionHeader {
    char Name[8];
    uint32_t VirtualSize;
    uint32_t VirtualAddress;
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;
    uint32_t PointerToRelocations;
    uint32_t PointerToLinenumbers;
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t Characteristics;
};

struct CoffRelocation {
    uint32_t VirtualAddress;
    uint32_t SymbolTableIndex;
    uint16_t Type;
};

struct CoffSymbol {
    union {
        char ShortName[8];
        struct {
            uint32_t Zeroes;
            uint32_t Offset;
        } LongName;
    } N;
    uint32_t Value;
    int16_t SectionNumber;
    uint16_t Type;
    uint8_t StorageClass;
    uint8_t NumberOfAuxSymbols;
};

struct DosHeader {
    uint16_t e_magic;
    uint16_t e_cblp;
    uint16_t e_cp;
    uint16_t e_crlc;
    uint16_t e_cparhdr;
    uint16_t e_minalloc;
    uint16_t e_maxalloc;
    uint16_t e_ss;
    uint16_t e_sp;
    uint16_t e_csum;
    uint16_t e_ip;
    uint16_t e_cs;
    uint16_t e_lfarlc;
    uint16_t e_ovno;
    uint16_t e_res[4];
    uint16_t e_oemid;
    uint16_t e_oeminfo;
    uint16_t e_res2[10];
    int32_t e_lfanew;
};

struct PeFileHeader {
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
};

struct DataDirectory {
    uint32_t VirtualAddress;
    uint32_t Size;
};

struct OptionalHeader64 {
    uint16_t Magic;
    uint8_t MajorLinkerVersion;
    uint8_t MinorLinkerVersion;
    uint32_t SizeOfCode;
    uint32_t SizeOfInitializedData;
    uint32_t SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint;
    uint32_t BaseOfCode;
    uint64_t ImageBase;
    uint32_t SectionAlignment;
    uint32_t FileAlignment;
    uint16_t MajorOperatingSystemVersion;
    uint16_t MinorOperatingSystemVersion;
    uint16_t MajorImageVersion;
    uint16_t MinorImageVersion;
    uint16_t MajorSubsystemVersion;
    uint16_t MinorSubsystemVersion;
    uint32_t Win32VersionValue;
    uint32_t SizeOfImage;
    uint32_t SizeOfHeaders;
    uint32_t CheckSum;
    uint16_t Subsystem;
    uint16_t DllCharacteristics;
    uint64_t SizeOfStackReserve;
    uint64_t SizeOfStackCommit;
    uint64_t SizeOfHeapReserve;
    uint64_t SizeOfHeapCommit;
    uint32_t LoaderFlags;
    uint32_t NumberOfRvaAndSizes;
    DataDirectory DataDirectories[16];
};

struct PeSectionHeader {
    char Name[8];
    uint32_t VirtualSize;
    uint32_t VirtualAddress;
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;
    uint32_t PointerToRelocations;
    uint32_t PointerToLinenumbers;
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t Characteristics;
};

struct ImportDescriptor {
    uint32_t OriginalFirstThunk;
    uint32_t TimeDateStamp;
    uint32_t ForwarderChain;
    uint32_t Name;
    uint32_t FirstThunk;
};
#pragma pack(pop)

// --- Internal Layout Structs ---

struct ObjSection {
    std::string name;
    CoffSectionHeader hdr{};
    std::vector<uint8_t> data;
    std::vector<CoffRelocation> relocs;
};

struct SymInfo {
    std::string name;
    CoffSymbol sym{};
};

struct OutSec {
    std::string name;
    uint32_t rva = 0;
    uint32_t raw = 0;
    uint32_t rawSize = 0;
    uint32_t virtSize = 0;
    uint32_t characteristics = 0;
    std::vector<uint8_t> data;
    std::vector<CoffRelocation> relocs;
};

class LinkerInstance {
public:
    LinkerInstance(const std::string& objPath, const std::string& exePath, const std::string& entrySymbol,
                   const std::unordered_map<std::string, std::string>& imports)
        : objPath(objPath), exePath(exePath), entrySymbol(entrySymbol), imports(imports) {}

    bool execute(std::string* errorOut) {
        if (!readObject(errorOut)) return false;
        if (!parseCoff(errorOut)) return false;

        prepareOutputSections();
        buildImportTable();
        buildThunks();
        layoutSections();
        resolveSymbols();

        if (!applyRelocations(errorOut)) return false;
        if (!emitPe(errorOut)) return false;

        return true;
    }

private:
    std::string objPath;
    std::string exePath;
    std::string entrySymbol;
    std::unordered_map<std::string, std::string> imports;

    std::vector<uint8_t> objRaw;
    std::vector<ObjSection> objSecs;
    std::vector<SymInfo> syms;

    std::vector<OutSec> outSecs;
    std::vector<int> coffSecToOut;
    std::unordered_map<std::string, uint32_t> symRva;

    // Import Table details
    uint32_t idataRva = 0;
    uint32_t idataSize = 0;
    uint32_t thunkRva = 0;
    std::map<std::string, uint32_t> iatRvaByFunc;
    std::map<std::string, uint32_t> thunkRvaByFunc;

    // Constants
    const uint32_t SectionAlignment = 0x1000;
    const uint32_t FileAlignment = 0x200;
    const uint64_t ImageBase = 0x00400000ULL;

    bool readObject(std::string* err) {
        std::ifstream f(objPath, std::ios::binary);
        if (!f) { if (err) *err = "Failed to open " + objPath; return false; }
        f.seekg(0, std::ios::end);
        size_t n = (size_t)f.tellg();
        f.seekg(0, std::ios::beg);
        objRaw.resize(n);
        f.read((char*)objRaw.data(), (std::streamsize)n);
        return true;
    }

    std::string readCoffSymName(const CoffSymbol& s) {
        if (s.N.LongName.Zeroes != 0) {
            size_t len = 0;
            while (len < 8 && s.N.ShortName[len] != 0) len++;
            return std::string(s.N.ShortName, s.N.ShortName + len);
        }
        uint32_t off = s.N.LongName.Offset;
        const CoffFileHeader* fh = (const CoffFileHeader*)objRaw.data();
        uint32_t strBase = fh->PointerToSymbolTable + fh->NumberOfSymbols * sizeof(CoffSymbol);
        if (strBase + 4 + off >= objRaw.size()) return "";
        const char* p = (const char*)(objRaw.data() + strBase + off);
        size_t maxLen = objRaw.size() - (strBase + off);
        size_t len = 0;
        while (len < maxLen && p[len] != 0) len++;
        return std::string(p, p + len);
    }

    bool parseCoff(std::string* err) {
        if (objRaw.size() < sizeof(CoffFileHeader)) { if (err) *err = "Object too small"; return false; }
        auto* fh = (const CoffFileHeader*)objRaw.data();
        if (fh->Machine != 0x8664) { if (err) *err = "Unsupported COFF machine (expected x64)"; return false; }

        size_t secOff = sizeof(CoffFileHeader) + fh->SizeOfOptionalHeader;
        for (uint16_t i = 0; i < fh->NumberOfSections; ++i) {
            ObjSection os;
            os.hdr = *(const CoffSectionHeader*)(objRaw.data() + secOff + i * sizeof(CoffSectionHeader));
            size_t n = 0; while (n < 8 && os.hdr.Name[n] != 0) n++;
            os.name = std::string(os.hdr.Name, os.hdr.Name + n);
            os.data.assign(objRaw.begin() + os.hdr.PointerToRawData, objRaw.begin() + os.hdr.PointerToRawData + os.hdr.SizeOfRawData);
            os.relocs.resize(os.hdr.NumberOfRelocations);
            for (uint16_t r = 0; r < os.hdr.NumberOfRelocations; ++r) {
                os.relocs[r] = *(const CoffRelocation*)(objRaw.data() + os.hdr.PointerToRelocations + r * sizeof(CoffRelocation));
            }
            objSecs.push_back(std::move(os));
        }

        syms.resize(fh->NumberOfSymbols);
        for (uint32_t i = 0; i < fh->NumberOfSymbols; ) {
            const CoffSymbol* s = (const CoffSymbol*)(objRaw.data() + fh->PointerToSymbolTable + i * sizeof(CoffSymbol));
            syms[i].sym = *s;
            syms[i].name = readCoffSymName(*s);
            i += 1 + s->NumberOfAuxSymbols;
        }
        return true;
    }

    void prepareOutputSections() {
        outSecs.reserve(objSecs.size() + 2); // + .idata and .thnk
        coffSecToOut.assign(objSecs.size() + 1, -1);
        for (size_t i = 0; i < objSecs.size(); ++i) {
            OutSec os;
            os.name = objSecs[i].name;
            os.data = objSecs[i].data;
            os.virtSize = (uint32_t)os.data.size();
            os.characteristics = objSecs[i].hdr.Characteristics;
            os.relocs = objSecs[i].relocs;
            coffSecToOut[i + 1] = (int)outSecs.size();
            outSecs.push_back(std::move(os));
        }
    }

    struct DllLayout {
        std::string dll;
        std::vector<std::string> funcs;
        size_t descOff, iltOff, iatOff, nameOff;
        std::vector<size_t> hintNameOffs;
    };
    std::vector<DllLayout> dllLayoutsCache;

    void buildImportTable() {
        std::map<std::string, std::vector<std::string>> dllToFuncs;
        for (const auto& kv : imports) dllToFuncs[kv.second].push_back(kv.first);
        for (auto& kv : dllToFuncs) std::sort(kv.second.begin(), kv.second.end());

        std::vector<uint8_t> idata;
        auto emit64 = [&](uint64_t v){ for(int i=0;i<8;i++) idata.push_back((uint8_t)((v>>(8*i))&0xFF)); };
        auto emitStrZ = [&](const std::string& s){ idata.insert(idata.end(), s.begin(), s.end()); idata.push_back(0); };
        auto padTo = [&](size_t a){ while (idata.size() % a) idata.push_back(0); };

        idata.resize((dllToFuncs.size() + 1) * sizeof(ImportDescriptor), 0);

        std::vector<DllLayout> dllLayouts;
        for (const auto& kv : dllToFuncs) {
            DllLayout d;
            d.dll = kv.first; d.funcs = kv.second;
            d.descOff = dllLayouts.size() * sizeof(ImportDescriptor);
            padTo(8); d.iltOff = idata.size();
            for (size_t i = 0; i < d.funcs.size() + 1; ++i) emit64(0);
            padTo(8); d.iatOff = idata.size();
            for (size_t i = 0; i < d.funcs.size() + 1; ++i) emit64(0);
            d.nameOff = idata.size(); emitStrZ(d.dll);
            for (const auto& f : d.funcs) {
                padTo(2); d.hintNameOffs.push_back(idata.size());
                idata.push_back(0); idata.push_back(0); // hint
                emitStrZ(f);
            }
            dllLayouts.push_back(std::move(d));
        }

        // Add .idata to output
        OutSec idsec;
        idsec.name = ".idata";
        idsec.data = std::move(idata);
        idsec.virtSize = (uint32_t)idsec.data.size();
        idsec.characteristics = 0xC0000040u; // read/write data
        outSecs.push_back(std::move(idsec));
        
        this->dllLayoutsCache = std::move(dllLayouts);
    }

    void buildThunks() {
        OutSec thsec;
        thsec.name = ".thnk";
        thsec.characteristics = 0x60000020u; // code/execute
        std::vector<std::string> importFuncs;
        for (const auto& kv : imports) importFuncs.push_back(kv.first);
        std::sort(importFuncs.begin(), importFuncs.end());

        for (const auto& fn : importFuncs) {
            thsec.data.push_back(0xFF); thsec.data.push_back(0x25); // jmp [rip+disp32]
            for (int i=0; i<4; ++i) thsec.data.push_back(0);
            while (thsec.data.size() % 16) thsec.data.push_back(0x90);
        }
        outSecs.push_back(std::move(thsec));
        this->sortedImportFuncs = std::move(importFuncs);
    }

    std::vector<std::string> sortedImportFuncs;

    void layoutSections() {
        uint32_t peHeaderSize = (uint32_t)(sizeof(DosHeader) + 0x40 + 4 + sizeof(PeFileHeader) + sizeof(OptionalHeader64) + outSecs.size() * sizeof(PeSectionHeader));
        uint32_t headersSize = (peHeaderSize + FileAlignment - 1) & ~(FileAlignment - 1);

        uint32_t curRva = (headersSize + SectionAlignment - 1) & ~(SectionAlignment - 1);
        uint32_t curRaw = headersSize;

        for (auto& s : outSecs) {
            s.rva = curRva;
            s.raw = curRaw;
            s.rawSize = (uint32_t)((s.data.size() + FileAlignment - 1) & ~(FileAlignment - 1));
            curRva = (uint32_t)((curRva + s.data.size() + SectionAlignment - 1) & ~(SectionAlignment - 1));
            curRaw += s.rawSize;
        }
        finalSizeOfImage = curRva;
        finalSizeOfHeaders = headersSize;
        finalCurRaw = curRaw;
    }

    uint32_t finalSizeOfImage, finalSizeOfHeaders, finalCurRaw;

    void resolveSymbols() {
        for (const auto& si : syms) {
            if (si.name.empty()) continue;
            if (si.sym.SectionNumber > 0) {
                int outIdx = coffSecToOut[(size_t)si.sym.SectionNumber];
                if (outIdx >= 0) symRva[si.name] = outSecs[outIdx].rva + si.sym.Value;
            }
        }

        // Patch .idata and .thnk
        int idataIdx = -1, thunkIdx = -1;
        for (int i=0; i<(int)outSecs.size(); ++i) {
            if (outSecs[i].name == ".idata") idataIdx = i;
            if (outSecs[i].name == ".thnk") thunkIdx = i;
        }

        idataRva = outSecs[idataIdx].rva;
        idataSize = (uint32_t)outSecs[idataIdx].data.size();
        auto& idataBytes = outSecs[idataIdx].data;
        
        auto write32 = [&](size_t off, uint32_t v){ for(int i=0;i<4;i++) idataBytes[off+i]=(uint8_t)(v>>(8*i)); };
        auto write64 = [&](size_t off, uint64_t v){ for(int i=0;i<8;i++) idataBytes[off+i]=(uint8_t)(v>>(8*i)); };

        for (const auto& d : dllLayoutsCache) {
            write32(d.descOff + 0, idataRva + (uint32_t)d.iltOff);
            write32(d.descOff + 12, idataRva + (uint32_t)d.nameOff);
            write32(d.descOff + 16, idataRva + (uint32_t)d.iatOff);
            for (size_t fi = 0; fi < d.funcs.size(); ++fi) {
                uint32_t hnRva = idataRva + (uint32_t)d.hintNameOffs[fi];
                write64(d.iltOff + fi * 8, hnRva);
                write64(d.iatOff + fi * 8, hnRva);
                iatRvaByFunc[d.funcs[fi]] = idataRva + (uint32_t)d.iatOff + (uint32_t)(fi * 8);
            }
        }

        thunkRva = outSecs[thunkIdx].rva;
        auto& thBytes = outSecs[thunkIdx].data;
        size_t off = 0;
        for (const auto& fn : sortedImportFuncs) {
            thunkRvaByFunc[fn] = thunkRva + (uint32_t)off;
            uint32_t targetIat = iatRvaByFunc[fn];
            int32_t disp = (int32_t)(targetIat - (thunkRva + (uint32_t)off + 6));
            std::memcpy(&thBytes[off + 2], &disp, 4);
            off += 6; while (off % 16) off++;
        }
    }

    bool applyRelocations(std::string* err) {
        for (auto& s : outSecs) {
            for (const auto& r : s.relocs) {
                const auto& sym = syms[r.SymbolTableIndex];
                uint32_t target = 0;
                if (imports.count(sym.name)) target = thunkRvaByFunc[sym.name];
                else target = symRva[sym.name];

                if (target == 0) { if (err) *err = "Unresolved symbol: " + sym.name; return false; }
                
                if (r.Type == 0x0004) { // REL32
                    int32_t addend = 0;
                    std::memcpy(&addend, &s.data[r.VirtualAddress], 4);
                    int32_t rel = (int32_t)(target + addend - (s.rva + r.VirtualAddress + 4));
                    std::memcpy(&s.data[r.VirtualAddress], &rel, 4);
                }
            }
        }
        return true;
    }

    bool emitPe(std::string* err) {
        std::vector<uint8_t> out(finalSizeOfHeaders, 0);
        DosHeader dh{};
        dh.e_magic = 0x5A4D; dh.e_lfanew = sizeof(DosHeader) + 0x40;
        std::memcpy(out.data(), &dh, sizeof(dh));
        std::memcpy(out.data() + sizeof(DosHeader), "This program cannot be run in DOS mode.\r\r\n$", 39);

        size_t peOff = (size_t)dh.e_lfanew;
        out[peOff] = 'P'; out[peOff+1] = 'E';

        PeFileHeader pfh{};
        pfh.Machine = 0x8664; pfh.NumberOfSections = (uint16_t)outSecs.size();
        pfh.SizeOfOptionalHeader = sizeof(OptionalHeader64);
        pfh.Characteristics = 0x0022; // EXE | LARGE_ADDRESS
        std::memcpy(out.data() + peOff + 4, &pfh, sizeof(pfh));

        auto itEntry = symRva.find(entrySymbol);
        if (itEntry == symRva.end()) { if (err) *err = "Entry symbol not found"; return false; }

        OptionalHeader64 oh{};
        oh.Magic = 0x20B; oh.AddressOfEntryPoint = itEntry->second;
        oh.ImageBase = ImageBase; oh.SectionAlignment = SectionAlignment; oh.FileAlignment = FileAlignment;
        oh.MajorOperatingSystemVersion = 6; oh.MajorSubsystemVersion = 6;
        oh.SizeOfImage = finalSizeOfImage; oh.SizeOfHeaders = finalSizeOfHeaders;
        oh.Subsystem = 3; oh.DllCharacteristics = 0x8100;
        oh.SizeOfStackReserve = 1 << 20; oh.SizeOfStackCommit = 1 << 12;
        oh.NumberOfRvaAndSizes = 16;
        oh.DataDirectories[1] = {idataRva, idataSize};
        std::memcpy(out.data() + peOff + 4 + sizeof(pfh), &oh, sizeof(oh));

        size_t shOff = peOff + 4 + sizeof(pfh) + sizeof(oh);
        for (size_t i = 0; i < outSecs.size(); ++i) {
            PeSectionHeader sh{};
            std::strncpy(sh.Name, outSecs[i].name.c_str(), 8);
            sh.VirtualSize = outSecs[i].virtSize; sh.VirtualAddress = outSecs[i].rva;
            sh.SizeOfRawData = outSecs[i].rawSize; sh.PointerToRawData = outSecs[i].raw;
            sh.Characteristics = outSecs[i].characteristics;
            std::memcpy(out.data() + shOff + i * sizeof(sh), &sh, sizeof(sh));
        }

        out.resize(finalCurRaw, 0);
        for (const auto& s : outSecs) std::memcpy(out.data() + s.raw, s.data.data(), s.data.size());

        std::ofstream of(exePath, std::ios::binary);
        if (!of) { if (err) *err = "Failed to write " + exePath; return false; }
        of.write((const char*)out.data(), (std::streamsize)out.size());
        return true;
    }
};

} // namespace

bool linkCoffObjectToPeExe(const std::string& objPath, const std::string& exePath, const std::string& entrySymbol,
                           const std::unordered_map<std::string, std::string>& imports, std::string* errorOut) {
    LinkerInstance linker(objPath, exePath, entrySymbol, imports);
    return linker.execute(errorOut);
}
