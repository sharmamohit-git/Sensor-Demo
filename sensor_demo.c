/*
 * sensor_demo.c - very basic OOP-style C demo
 * Pipeline: Sensor -> RingBuffer -> Processor (moving average)
 *
 */
#include <stdio.h>
#include <stdlib.h>

/* ---------- Class: RingBuffer (data structure + memory management) ---------- */
typedef struct {
    int    *data;      /* allocated once, never in the hot path */
    size_t  capacity;
    size_t  head, tail, count;
    size_t  dropped;   /* overflow policy: drop oldest, but count it */
} RingBuffer;

RingBuffer *RingBuffer_create(size_t capacity) {
    RingBuffer *rb = malloc(sizeof *rb);
    if (!rb) return NULL;
    rb->data = malloc(capacity * sizeof *rb->data);
    if (!rb->data) { free(rb); return NULL; }
    rb->capacity = capacity;
    rb->head = rb->tail = rb->count = rb->dropped = 0;
    return rb;
}

void RingBuffer_destroy(RingBuffer *rb) {
    if (!rb) return;
    free(rb->data);
    free(rb);
}

void RingBuffer_push(RingBuffer *rb, int value) {
    if (rb->count == rb->capacity) {           /* full: drop oldest */
        rb->tail = (rb->tail + 1) % rb->capacity;
        rb->count--;
        rb->dropped++;
    }
    rb->data[rb->head] = value;
    rb->head = (rb->head + 1) % rb->capacity;
    rb->count++;
}

int RingBuffer_pop(RingBuffer *rb, int *out) {
    if (rb->count == 0) return 0;              /* empty */
    *out = rb->data[rb->tail];
    rb->tail = (rb->tail + 1) % rb->capacity;
    rb->count--;
    return 1;
}

/* ---------- Class: Sensor (base "interface") ---------- */
typedef struct Sensor Sensor;
struct Sensor {
    int  (*read)(Sensor *self, int *out);      /* returns 0 on success, -1 on fault */
    void (*destroy)(Sensor *self);
};

/* ---------- Class: SimSensor (derived from Sensor) ---------- */
typedef struct {
    Sensor base;        /* must be first: allows safe cast to Sensor* */
    int    tick;
} SimSensor;

static int SimSensor_read(Sensor *self, int *out) {
    SimSensor *s = (SimSensor *)self;
    s->tick++;
    if (s->tick % 7 == 0) return -1;           /* simulate a hardware fault */
    *out = 20 + (s->tick * 3) % 10;            /* fake temperature value */
    return 0;
}

static void SimSensor_destroy(Sensor *self) { free(self); }

Sensor *SimSensor_create(void) {
    SimSensor *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->base.read    = SimSensor_read;
    s->base.destroy = SimSensor_destroy;
    return &s->base;
}

/* ---------- Class: Processor (moving average of last N samples) ---------- */
#define WINDOW 4
typedef struct {
    int window[WINDOW];
    int filled, next, sum;
} Processor;

void Processor_init(Processor *p) { *p = (Processor){0}; }

double Processor_add(Processor *p, int value) {
    if (p->filled == WINDOW) p->sum -= p->window[p->next];
    else p->filled++;
    p->window[p->next] = value;
    p->sum += value;
    p->next = (p->next + 1) % WINDOW;
    return (double)p->sum / p->filled;
}

/* ---------- main: wires the pipeline together ---------- */
int main(void) {
    Sensor     *sensor = SimSensor_create();
    RingBuffer *buffer = RingBuffer_create(5);
    if (!sensor || !buffer) {
        fprintf(stderr, "allocation failed\n");
        if (sensor) sensor->destroy(sensor);
        RingBuffer_destroy(buffer);
        return EXIT_FAILURE;
    }

    Processor proc;
    Processor_init(&proc);
    int faults = 0;

    for (int i = 0; i < 20; i++) {
        int value;
        if (sensor->read(sensor, &value) != 0) {   /* failure handling */
            faults++;
            fprintf(stderr, "sample %2d: sensor fault, skipped\n", i);
            continue;
        }
        RingBuffer_push(buffer, value);            /* acquisition stage */

        if (i % 3 == 0) {                          /* consumer is slower */
            int v;
            while (RingBuffer_pop(buffer, &v))     /* processing stage */
                printf("sample %2d: raw=%2d avg=%.2f\n", i, v, Processor_add(&proc, v));
        }
    }

    printf("faults=%d dropped=%zu\n", faults, buffer->dropped);

    sensor->destroy(sensor);                       /* cleanup, no leaks */
    RingBuffer_destroy(buffer);
    return EXIT_SUCCESS;
}
