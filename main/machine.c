#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <machine.h>

extern struct machine __machines_begin, __machines_end;
struct machine *machine;

bool machine_init(char *name)
{
	struct machine *m;
	for (m = &__machines_begin; m < &__machines_end; m++)
		if (!strcmp(name, m->name))
			machine = m;

	/* Exit if machine has not been found */
	if (!machine) {
		fprintf(stderr, "Machine \"%s\" not recognized!\n", name);
		return false;
	}

	/* Display machine name and description */
	fprintf(stdout, "Machine: %s (%s)\n", machine->name,
		machine->description);

	if (machine->init)
		return machine->init();
	return true;
}

void machine_deinit()
{
	cpu_remove_all();
	controller_remove_all();
	memory_region_remove_all();
	if (machine->deinit)
		machine->deinit();
}

