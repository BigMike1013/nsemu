/* nsemu - LGPL - Copyright 2017 rkx1209<rkx1209dev@gmail.com> */
#include <sys/mman.h>
#include "Nsemu.hpp"

RAMBlock::RAMBlock (std::string _name, uint64_t _addr, size_t _length, int _perm) : block(nullptr){
	int page = getpagesize ();
	name = _name;
	length = _length;
	perm = _perm;
	if (addr & (page - 1)) {
		addr = addr & ~(page - 1);
	}
	addr = _addr;
}
RAMBlock::RAMBlock(std::string _name, uint64_t _addr, size_t _length, uint8_t *raw, int _perm) {
	int page = getpagesize ();
	name = _name;
	length = _length;
	perm = _perm;
	if (addr & (page - 1)) {
		addr = addr & ~(page - 1);
	}
	addr = _addr;
        block = raw;
}

namespace Memory
{
uint64_t heap_base = 0x9000000;
uint64_t heap_size = 0x0;
uint8_t *pRAM;	// XXX: Replace raw pointer to View wrapper.
size_t ram_size = 0x10000000;
uint64_t straight_max = heap_base + heap_size;
std::vector<RAMBlock*> regions;
static RAMBlock mem_map_straight[] =
{
	RAMBlock (".text", 0x0, 0x0ffffff, PROT_READ | PROT_WRITE | PROT_EXEC),
	RAMBlock (".rdata", 0x1000000, 0x0ffffff, PROT_READ | PROT_WRITE),
	RAMBlock (".data", 0x2000000, 0x0ffffff, PROT_READ | PROT_WRITE),
	RAMBlock ("[stack]", 0x3000000, 0x5ffffff, PROT_READ | PROT_WRITE),
};

static bool inline IsStraight(uint64_t addr, size_t len) {
        return addr + len <= straight_max;
}

static RAMBlock* FindRamBlock(uint64_t addr, size_t len) {
        for (int i = 0; i < regions.size(); i++) {
                if (regions[i]->addr <= addr && addr + len <= regions[i]->addr + regions[i]->length) {
                        return regions[i];
                }
        }
        return nullptr;
}

static void AddAnonStraight(uint64_t addr, size_t len, int perm) {
        //ns_print("Add anonymous fixed region [0x%lx, %d]\n", addr, len);
        RAMBlock *new_ram = new RAMBlock("[anon]", addr, len, perm)        ;
        regions.push_back(new_ram);
}

static void AddAnonRamBlock(uint64_t addr, size_t len, int perm) {
        uint8_t *raw = new uint8_t[len];
        if (!raw) {
                ns_abort("Failed to allocate new RAM Block\n");
        }
        RAMBlock *new_ram = new RAMBlock("[anon]", addr, len, raw, perm);
        //ns_print("Add anonymous region [0x%lx, %d]\n", new_ram->addr, new_ram->length);
        regions.push_back(new_ram);
}

void AddMemmap(uint64_t addr, size_t len) {
        if (IsStraight(addr, len)) {
                /* Within straight regions */
                AddAnonStraight(addr, len, PROT_READ | PROT_WRITE);
                return;
        }
        // if (ExistOverlap(addr, len))
        /* TODO Detect overlapped areas */

        /* Necessary to extend memory area */
        AddAnonRamBlock(addr, len, PROT_READ | PROT_WRITE);
}

void DelMemmap(uint64_t addr, size_t len) {
        auto it = regions.begin();
        while (it != regions.end()) {
                RAMBlock *ram = *it;
                if (addr <= ram->addr && ram->addr + ram->length <= addr + len) {
                        delete ram;
                        it = regions.erase(it);
                } else {
                        ++it;
                }
        }
}

void InitMemmap(Nsemu *nsemu) {
        void *data;
	if ((data = mmap (nullptr, ram_size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED) {
	 	ns_abort ("Failed to allocate host memory\n");
	}
        pRAM = (uint8_t *) data;
        for (int i = 0; i < sizeof(mem_map_straight) / sizeof(RAMBlock); i++) {
                regions.push_back(&mem_map_straight[i]);
        }
}

std::list<std::tuple<uint64_t,uint64_t, int>> GetRegions() {
        std::list<std::tuple<uint64_t,uint64_t, int>> ret;
        uint64_t last;
        for (int i = 0; i < regions.size(); i++) {
                uint64_t addr = regions[i]->addr;
                size_t length = regions[i]->length;
                int perm = regions[i]->perm;
                ret.push_back(make_tuple(addr, addr + length, perm));
                last = addr + length + 1;
        }
        ret.push_back(make_tuple(last, 0xFFFFFFFFFFFFFFFF, -1));
        return ret;
}

void *GetRawPtr(uint64_t gpa, size_t len) {
        void *emu_mem;
        if (IsStraight(gpa, len)) {
                emu_mem = (void *)&pRAM[gpa];
        }
        else {
                RAMBlock *ram = FindRamBlock (gpa, len);
                if (!ram) {
                        return nullptr;
                }
                emu_mem = (void *)&ram->block[gpa - ram->addr];
        }
        return emu_mem;
}

static bool _CopyMemEmu(void *data, uint64_t gpa, size_t len, bool load) {
        void *emu_mem = GetRawPtr (gpa, len);
        if (!emu_mem) {
                return false;
        }
	if (load) {
		memcpy (emu_mem, data, len);
	} else {
		memcpy (data, emu_mem, len);
	}
	return true;
}

bool CopytoEmu(Nsemu *nsemu, void *data, uint64_t gpa, size_t len) {
	return _CopyMemEmu (data, gpa, len, true);
}

bool CopyfromEmu(Nsemu *nsemu, void *data, uint64_t gpa, size_t len) {
	return _CopyMemEmu (data, gpa, len, false);
}

}
