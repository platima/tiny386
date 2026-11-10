/*
 * One ide.c behind a table of its entry points (ide_side.c), so that a test
 * can drive two of them side by side.  The disks are the test's own: every
 * read and write of a sector goes to ide_model_rw().
 */
#ifndef IDE_SIDE_H
#define IDE_SIDE_H
#include <stdint.h>

struct ide_model;
/* n sectors at sector: 0, or -1 if refused; write 0 reads into buf */
int ide_model_rw(struct ide_model *m, uint64_t sector, uint8_t *buf, int n, int write);
int64_t ide_model_sectors(struct ide_model *m);

struct ide_side {
    const char *what;
    int gather;                 /* sectors gathered at most (0: none) */
    int read_ahead;             /* sectors read ahead at most (0: none) */
    /* an IDE interface with these disks (NULL: none) as drives 0 and 1 */
    void *(*create)(struct ide_model *d0, struct ide_model *d1, void *pic,
                    void (*set_irq)(void *pic, int irq, int level));
    void (*destroy)(void *ifs);
    void (*ioport_write)(void *ifs, uint32_t addr, uint32_t val);
    uint32_t (*ioport_read)(void *ifs, uint32_t addr);
    void (*cmd_write)(void *ifs, uint32_t val);
    uint32_t (*status_read)(void *ifs);
    void (*data_writew)(void *ifs, uint32_t val);
    uint32_t (*data_readw)(void *ifs);
    void (*data_writel)(void *ifs, uint32_t val);
    uint32_t (*data_readl)(void *ifs);
    int (*data_write_string)(void *ifs, uint8_t *buf, int size, int count);
    int (*data_read_string)(void *ifs, uint8_t *buf, int size, int count);
    /* v5.3: the front end's flush (ide_write_pending()); NULL for an ide.c
       without it */
    void (*write_pending)(void *ifs);
};
#endif
