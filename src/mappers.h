#ifndef _MAPPERS_H_
#define _MAPPERS_H_

#include <stdbool.h>

typedef struct
{
    const char *name;
    void (*init)(void);
    void (*translateAddress)(uint32_t address, uint16_t *offset);
    bool (*detect)(void);
} Mapper_t;

extern Mapper_t mappers[2];

extern void setMapper(Mapper_t *mapper);
extern Mapper_t *getCurrentMapper(void);
extern char *getCurrentMapperName(void);
extern void translateAddress(uint32_t address, uint16_t *offset);
extern uint8_t detectMapper(void);

#endif /* _MAPPERS_H_ */
