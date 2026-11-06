#ifndef SERVICES_H
#define SERVICES_H
int servicio_a(int w_ai, int r_ia, int w_spans, int n_solicitudes);
int servicio_inventario(int r_ai, int w_ib, int r_bi, int w_ia,
                        int w_spans);
int servicio_b(int r_ib, int w_bi, int w_spans);
#endif