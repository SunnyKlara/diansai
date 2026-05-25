/**
 * @file    chua_oscillator.c
 * @brief   蔡氏混沌 RK4
 */
#include "chua_oscillator.h"
#include "../config.h"

static float chua_g(float x, float m0, float m1)
{
    float a = x + 1.0f, b = x - 1.0f;
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    return m1 * x + 0.5f * (m0 - m1) * (a - b);
}

void chua_init(chua_t *c, float alpha, float beta, float gamma,
               float m0, float m1, float dt)
{
    if (!c) return;
    c->x = 0.1f; c->y = 0.0f; c->z = 0.0f;
    c->alpha = alpha; c->beta = beta; c->gamma = gamma;
    c->m0 = m0; c->m1 = m1; c->dt = dt;
}

void chua_reset(chua_t *c, float x0, float y0, float z0)
{
    if (!c) return;
    c->x = x0; c->y = y0; c->z = z0;
}

static void chua_deriv(const chua_t *c, float x, float y, float z,
                       float *dx, float *dy, float *dz)
{
    *dx = c->alpha * (y - x - chua_g(x, c->m0, c->m1));
    *dy = x - y + z;
    *dz = -c->beta * y - c->gamma * z;
}

void chua_step(chua_t *c)
{
    if (!c) return;
    float k1x, k1y, k1z;
    chua_deriv(c, c->x, c->y, c->z, &k1x, &k1y, &k1z);

    float k2x, k2y, k2z;
    chua_deriv(c, c->x + 0.5f*c->dt*k1x, c->y + 0.5f*c->dt*k1y, c->z + 0.5f*c->dt*k1z,
               &k2x, &k2y, &k2z);

    float k3x, k3y, k3z;
    chua_deriv(c, c->x + 0.5f*c->dt*k2x, c->y + 0.5f*c->dt*k2y, c->z + 0.5f*c->dt*k2z,
               &k3x, &k3y, &k3z);

    float k4x, k4y, k4z;
    chua_deriv(c, c->x + c->dt*k3x, c->y + c->dt*k3y, c->z + c->dt*k3z,
               &k4x, &k4y, &k4z);

    c->x += c->dt * (k1x + 2*k2x + 2*k3x + k4x) / 6.0f;
    c->y += c->dt * (k1y + 2*k2y + 2*k3y + k4y) / 6.0f;
    c->z += c->dt * (k1z + 2*k2z + 2*k3z + k4z) / 6.0f;
}
