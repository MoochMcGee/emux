#include <stdlib.h>
#include <string.h>
#include <clock.h>
#include <cpu.h>
#include <log.h>
#include <memory.h>
#include <resource.h>

#define NUM_REGISTERS		32
#define INITIAL_PC		0xBFC00000

union cache_control {
	uint32_t raw;
	struct {
		uint32_t LOCK:1;	/* Lock Mode */
		uint32_t INV:1;		/* Invalidate Mode */
		uint32_t TAG:1;		/* Tag Test Mode */
		uint32_t RAM:1;		/* Scratchpad RAM */
		uint32_t DBLKSZ:2;	/* D-Cache Refill Size */
		uint32_t reserved1:1;
		uint32_t DS:1;		/* Enable D-Cache */
		uint32_t IBLKSZ:2;	/* I-Cache Refill Size */
		uint32_t IS0:1;		/* Enable I-Cache Set 0 */
		uint32_t IS1:1;		/* Enable I-Cache Set 1 */
		uint32_t INTP:1;	/* Interrupt Polarity */
		uint32_t RDPRI:1;	/* Enable Read Priority */
		uint32_t NOPAD:1;	/* No Wait State */
		uint32_t BGNT:1;	/* Enable Bus Grant */
		uint32_t LDSCH:1;	/* Enable Load Scheduling */
		uint32_t NOSTR:1;	/* No Streaming */
		uint32_t reserved2:14;
	};
};

/* I-Type (Immediate) instruction */
struct i_type {
	uint32_t immediate:16;
	uint32_t rt:5;
	uint32_t rs:5;
	uint32_t opcode:6;
};

/* J-Type (Jump) instruction */
struct j_type {
	uint32_t target:26;
	uint32_t opcode:6;
};

/* R-Type (Register) instruction */
struct r_type {
	uint32_t funct:6;
	uint32_t shamt:5;
	uint32_t rd:5;
	uint32_t rt:5;
	uint32_t rs:5;
	uint32_t opcode:6;
};

/* Special instruction */
struct special {
	uint32_t opcode:6;
	uint32_t reserved:26;
};

/* Branch condition instruction */
struct bcond {
	uint32_t reserved:16;
	uint32_t opcode:5;
	uint32_t reserved2:11;
};

/* Coprocessor instruction */
struct cop {
	uint32_t reserved:21;
	uint32_t opcode:5;
	uint32_t reserved2:6;
};

union instruction {
	uint32_t raw;
	struct {
		uint32_t reserved:26;
		uint32_t opcode:6;
	};
	struct i_type i_type;
	struct j_type j_type;
	struct r_type r_type;
	struct special special;
	struct bcond bcond;
	struct cop cop;
};

struct r3051 {
	union {
		uint32_t R[NUM_REGISTERS];
		struct {
			uint32_t zr;
			uint32_t at;
			uint32_t v0;
			uint32_t v1;
			uint32_t a0;
			uint32_t a1;
			uint32_t a2;
			uint32_t a3;
			uint32_t t0;
			uint32_t t1;
			uint32_t t2;
			uint32_t t3;
			uint32_t t4;
			uint32_t t5;
			uint32_t t6;
			uint32_t t7;
			uint32_t s0;
			uint32_t s1;
			uint32_t s2;
			uint32_t s3;
			uint32_t s4;
			uint32_t s5;
			uint32_t s6;
			uint32_t s7;
			uint32_t t8;
			uint32_t t9;
			uint32_t k0;
			uint32_t k1;
			uint32_t gp;
			uint32_t sp;
			uint32_t fp;
			uint32_t ra;
		};
	};
	uint32_t HI;
	uint32_t LO;
	uint32_t PC;
	union instruction instruction;
	union cache_control cache_ctrl;
	int bus_id;
	struct clock clock;
	struct region cache_ctrl_region;
};

static bool r3051_init(struct cpu_instance *instance);
static void r3051_reset(struct cpu_instance *instance);
static void r3051_deinit(struct cpu_instance *instance);
static void r3051_fetch(struct r3051 *cpu);
static void r3051_tick(struct r3051 *cpu);

#define DEFINE_MEM_READ(ext, type) \
	static type mem_read##ext(struct r3051 *cpu, address_t a) \
	{ \
		return memory_read##ext(cpu->bus_id, a); \
	}

#define DEFINE_MEM_WRITE(ext, type) \
	static void mem_write##ext(struct r3051 *cpu, type data, address_t a) \
	{ \
		memory_write##ext(cpu->bus_id, data, a); \
	}

DEFINE_MEM_READ(b, uint8_t)
DEFINE_MEM_WRITE(b, uint8_t)
DEFINE_MEM_READ(w, uint16_t)
DEFINE_MEM_WRITE(w, uint16_t)
DEFINE_MEM_READ(l, uint32_t)
DEFINE_MEM_WRITE(l, uint32_t)

void r3051_fetch(struct r3051 *cpu)
{
	/* Fetch instruction */
	cpu->instruction.raw = mem_readl(cpu, cpu->PC);
	cpu->PC += 4;
}

void r3051_tick(struct r3051 *cpu)
{
	/* Fetch instruction */
	r3051_fetch(cpu);

	/* Execute instruction */
	switch (cpu->instruction.opcode) {
	default:
		LOG_W("Unknown instruction (%08x)!\n", cpu->instruction.raw);
		break;
	}

	/* Always consume one cycle */
	clock_consume(1);
}

bool r3051_init(struct cpu_instance *instance)
{
	struct r3051 *cpu;
	struct resource *res;

	/* Allocate r3051 structure and set private data */
	cpu = calloc(1, sizeof(struct r3051));
	instance->priv_data = cpu;

	/* Save bus ID */
	cpu->bus_id = instance->bus_id;

	/* Add CPU clock */
	res = resource_get("clk",
		RESOURCE_CLK,
		instance->resources,
		instance->num_resources);
	cpu->clock.rate = res->data.clk;
	cpu->clock.data = cpu;
	cpu->clock.tick = (clock_tick_t)r3051_tick;
	clock_add(&cpu->clock);

	/* Add cache control region */
	res = resource_get("cache_control",
		RESOURCE_MEM,
		instance->resources,
		instance->num_resources);
	cpu->cache_ctrl_region.area = res;
	cpu->cache_ctrl_region.mops = &ram_mops;
	cpu->cache_ctrl_region.data = &cpu->cache_ctrl.raw;
	memory_region_add(&cpu->cache_ctrl_region);

	return true;
}

void r3051_reset(struct cpu_instance *instance)
{
	struct r3051 *cpu = instance->priv_data;

	/* Intialize processor data */
	cpu->PC = INITIAL_PC;
	memset(cpu->R, 0, NUM_REGISTERS * sizeof(uint32_t));

	/* Enable clock */
	cpu->clock.enabled = true;
}

void r3051_deinit(struct cpu_instance *instance)
{
	free(instance->priv_data);
}

CPU_START(r3051)
	.init = r3051_init,
	.reset = r3051_reset,
	.deinit = r3051_deinit
CPU_END

