#include <stdlib.h>
#include <cmdline.h>
#include <controller.h>
#include <cpu.h>
#include <file.h>
#include <log.h>
#include <machine.h>
#include <memory.h>
#include <resource.h>
#include <util.h>

/* Clock frequencies */
#define CPU_CLOCK_RATE	33868800
#define GPU_CLOCK_RATE	53690000
#define DMA_CLOCK_RATE	CPU_CLOCK_RATE

/* Bus definitions */
#define CPU_BUS_ID	0

/* Memory map */
#define RAM_START		0x00000000
#define RAM_END			0x00200000
#define INT_CONTROL_START	0x1F801070
#define INT_CONTROL_END		0x1F801074
#define DMA_START		0x1F801080
#define DMA_END			0x1F8010F4
#define GPU_START		0x1F801810
#define GPU_END			0x1F801814
#define BIOS_START		0x1FC00000
#define BIOS_END		0x1FC7FFFF
#define CACHE_CONTROL		0xFFFE0130

/* Memory sizes */
#define RAM_SIZE		KB(2048)
#define BIOS_SIZE		KB(512)

/* DMA channels */
#define GPU_DMA_CHANNEL		2

/* IRQ numbers */
#define DMA_IRQ			3

struct psx_data {
	uint8_t ram[RAM_SIZE];
	uint8_t *bios;
	struct region ram_region;
	struct region bios_region;
};

static bool psx_init(struct machine *machine);
static void psx_deinit(struct machine *machine);

/* Command-line parameters */
static char *bios_path = "scph5501.bin";
PARAM(bios_path, string, "bios", "psx", "PSX BIOS path")

/* RAM area */
static struct resource ram_area =
	MEM("ram", CPU_BUS_ID, RAM_START, RAM_END);

/* BIOS area */
static struct resource bios_area =
	MEM("bios", CPU_BUS_ID, BIOS_START, BIOS_END);

/* R3051 CPU */
static struct resource cpu_resources[] = {
	CLK("clk", CPU_CLOCK_RATE),
	MEM("int_control", CPU_BUS_ID, INT_CONTROL_START, INT_CONTROL_END),
	MEM("cache_control", CPU_BUS_ID, CACHE_CONTROL, CACHE_CONTROL)
};

static struct cpu_instance cpu_instance = {
	.cpu_name = "r3051",
	.bus_id = CPU_BUS_ID,
	.resources = cpu_resources,
	.num_resources = ARRAY_SIZE(cpu_resources)
};

/* DMA */
static struct resource dma_resources[] = {
	MEM("mem", CPU_BUS_ID, DMA_START, DMA_END),
	IRQ("irq", DMA_IRQ),
	CLK("clk", DMA_CLOCK_RATE)
};

static struct controller_instance dma_instance = {
	.controller_name = "psx_dma",
	.bus_id = CPU_BUS_ID,
	.resources = dma_resources,
	.num_resources = ARRAY_SIZE(dma_resources)
};

/* GPU */
static struct resource gpu_resources[] = {
	CLK("clk", GPU_CLOCK_RATE),
	MEM("mem", CPU_BUS_ID, GPU_START, GPU_END),
	DMA("dma", GPU_DMA_CHANNEL)
};

static struct controller_instance gpu_instance = {
	.controller_name = "gpu",
	.bus_id = CPU_BUS_ID,
	.resources = gpu_resources,
	.num_resources = ARRAY_SIZE(gpu_resources)
};

bool psx_init(struct machine *machine)
{
	struct psx_data *data;

	/* Create machine data structure */
	data = calloc(1, sizeof(struct psx_data));
	machine->priv_data = data;

	/* Map BIOS */
	data->bios = file_map(PATH_SYSTEM,
		bios_path,
		0,
		BIOS_SIZE);
	if (!data->bios) {
		LOG_E("Could not map BIOS!\n");
		free(data);
		return false;
	}

	/* Initialize RAM region */
	data->ram_region.area = &ram_area;
	data->ram_region.mops = &ram_mops;
	data->ram_region.data = data->ram;
	memory_region_add(&data->ram_region);

	/* Initialize BIOS region */
	data->bios_region.area = &bios_area;
	data->bios_region.mops = &rom_mops;
	data->bios_region.data = data->bios;
	memory_region_add(&data->bios_region);

	/* Add controllers and CPU */
	if (!cpu_add(&cpu_instance) ||
		!controller_add(&dma_instance) ||
		!controller_add(&gpu_instance)) {
		file_unmap(data->bios, BIOS_SIZE);
		free(data);
		return false;
	}

	return true;
}

void psx_deinit(struct machine *machine)
{
	struct psx_data *data = machine->priv_data;

	file_unmap(data->bios, BIOS_SIZE);
	free(machine->priv_data);
}

MACHINE_START(psx, "Sony PlayStation")
	.init = psx_init,
	.deinit = psx_deinit
MACHINE_END

