// SPDX-License-Identifier: GPL-2.0
/*
 * redmi_vsp_test: prueft /dev/sprd_vsp (Etappe 3c) von der Programmseite aus,
 * so wie Unisocs Bibliothek die Datei benutzt - aber ohne dem VSP Arbeit zu
 * geben. In die Register wird nichts geschrieben, nur gelesen.
 *
 *   VERSION, ACQUAIRE, ENABLE, mmap der Register (lesen), GET_IOVA mit einem
 *   dma-buf aus /dev/dma_heap/system, FREE_IOVA, RESET, COMPLETE (muss nach
 *   200 ms mit Zeitueberschreitung zurueckkommen), DISABLE, RELEASE.
 *
 * Bauen:  aarch64-linux-gnu-gcc -static -O2 -Wall -o redmi_vsp_test redmi_vsp_test.c
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define SPRD_VSP_IOCTL_MAGIC	'm'
#define VSP_ENABLE		_IO(SPRD_VSP_IOCTL_MAGIC, 3)
#define VSP_DISABLE		_IO(SPRD_VSP_IOCTL_MAGIC, 4)
#define VSP_ACQUAIRE		_IO(SPRD_VSP_IOCTL_MAGIC, 5)
#define VSP_RELEASE		_IO(SPRD_VSP_IOCTL_MAGIC, 6)
#define VSP_COMPLETE		_IO(SPRD_VSP_IOCTL_MAGIC, 7)
#define VSP_RESET		_IO(SPRD_VSP_IOCTL_MAGIC, 8)
#define VSP_VERSION		_IO(SPRD_VSP_IOCTL_MAGIC, 10)
#define VSP_GET_IOMMU_STATUS	_IO(SPRD_VSP_IOCTL_MAGIC, 13)

struct vsp_iommu_map_data {
	int fd;
	uint64_t size;
	uint64_t iova_addr;
	uint8_t need_cache_sync;
};
#define VSP_GET_IOVA	_IOWR(SPRD_VSP_IOCTL_MAGIC, 11, struct vsp_iommu_map_data)
#define VSP_FREE_IOVA	_IOW(SPRD_VSP_IOCTL_MAGIC, 12, struct vsp_iommu_map_data)

/* aus linux/dma-heap.h */
struct dma_heap_allocation_data {
	uint64_t len;
	uint32_t fd;
	uint32_t fd_flags;
	uint64_t heap_flags;
};
#define DMA_HEAP_IOCTL_ALLOC _IOWR('H', 0x0, struct dma_heap_allocation_data)

#define MAP_GROESSE	0xc000
#define PUFFER		(1024 * 1024)

static int fehler;

static void pruef(const char *was, int ok, const char *info)
{
	printf("%-28s %s  %s\n", was, ok ? "OK    " : "FEHLER", info);
	if (!ok)
		fehler++;
}

int main(void)
{
	char info[160];
	unsigned int wert = 0;
	int fd, heap, r;
	volatile uint32_t *regs;

	fd = open("/dev/sprd_vsp", O_RDWR);
	if (fd < 0) {
		perror("open /dev/sprd_vsp");
		return 1;
	}

	r = ioctl(fd, VSP_VERSION, &wert);
	snprintf(info, sizeof(info), "Version %u (erwartet 20 = SHARKL6)", wert);
	pruef("VSP_VERSION", r == 0 && wert == 20, info);

	r = ioctl(fd, VSP_GET_IOMMU_STATUS, 0);
	pruef("VSP_GET_IOMMU_STATUS", r == 0, "IOMMU vorhanden");

	r = ioctl(fd, VSP_ACQUAIRE, 0);
	pruef("VSP_ACQUAIRE", r == 0, r ? strerror(errno) : "Hardware reserviert");

	r = ioctl(fd, VSP_ENABLE, 0);
	pruef("VSP_ENABLE", r == 0, r ? strerror(errno) : "Takte an");

	regs = mmap(NULL, MAP_GROESSE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (regs == MAP_FAILED) {
		pruef("mmap Register", 0, strerror(errno));
	} else {
		snprintf(info, sizeof(info), "MMU VERSION 0x%08x CFG 0x%08x, GLB MASK 0x%08x",
			 regs[0x00 / 4], regs[0x04 / 4], regs[0x1004 / 4]);
		pruef("mmap Register (lesen)", regs[0] == 0x100 && regs[1] == 0xb, info);
		munmap((void *)regs, MAP_GROESSE);
	}

	heap = open("/dev/dma_heap/system", O_RDONLY);
	if (heap < 0) {
		snprintf(info, sizeof(info), "/dev/dma_heap/system: %s - Test uebersprungen",
			 strerror(errno));
		printf("%-28s %s  %s\n", "VSP_GET_IOVA", "--    ", info);
	} else {
		struct dma_heap_allocation_data a = {
			.len = PUFFER, .fd_flags = O_RDWR | O_CLOEXEC,
		};

		r = ioctl(heap, DMA_HEAP_IOCTL_ALLOC, &a);
		if (r) {
			pruef("dma-heap Puffer", 0, strerror(errno));
		} else {
			struct vsp_iommu_map_data m = { .fd = (int)a.fd };

			r = ioctl(fd, VSP_GET_IOVA, &m);
			snprintf(info, sizeof(info), "1 MiB: IOVA 0x%llx, %llu Bytes",
				 (unsigned long long)m.iova_addr,
				 (unsigned long long)m.size);
			pruef("VSP_GET_IOVA", r == 0 && m.iova_addr >= 0x20000000 &&
			      m.iova_addr < 0x30000000 && m.size == PUFFER,
			      r ? strerror(errno) : info);
			if (r == 0) {
				r = ioctl(fd, VSP_FREE_IOVA, &m);
				pruef("VSP_FREE_IOVA", r == 0, r ? strerror(errno) : "freigegeben");
			}
			close(a.fd);
		}
		close(heap);
	}

	r = ioctl(fd, VSP_RESET, 0);
	pruef("VSP_RESET", r == 0, r ? strerror(errno) : "zurueckgesetzt");

	wert = 0;
	r = ioctl(fd, VSP_COMPLETE, &wert);
	snprintf(info, sizeof(info), "ohne Auftrag: %s, Status 0x%08x (erwartet Bit 31)",
		 r ? strerror(errno) : "kein Fehler?!", wert);
	pruef("VSP_COMPLETE", r != 0 && errno == ETIMEDOUT && (wert & 0x80000000u), info);

	r = ioctl(fd, VSP_DISABLE, 0);
	pruef("VSP_DISABLE", r == 0, r ? strerror(errno) : "Takte aus");
	r = ioctl(fd, VSP_RELEASE, 0);
	pruef("VSP_RELEASE", r == 0, r ? strerror(errno) : "Hardware frei");

	close(fd);
	printf("\n%s (%d Fehler)\n", fehler ? "NICHT bestanden" : "bestanden", fehler);
	return fehler ? 1 : 0;
}
