#ifndef CARSMOOTH_H
#define CARSMOOTH_H
#include <stdint.h>
// verts: x y z u v per vertex (5 floats); idx: triangle list, indices relative to verts.
typedef struct { const float *verts; int nverts; const uint16_t *idx; int nidx; } CsIn;
// out->verts: nverts * 5 floats (x y z u v) followed by nverts * 3 floats of normals. The caller frees out->verts and out->idx.
typedef struct { float *verts; int nverts; uint16_t *idx; int nidx; } CsOut;
// Curves the triangles with PN patches subdivided `level` times (lowered until the vertices fit 16 bits). 0 = left as it is.
int cs_smooth(const CsIn *in, CsOut *out, int level);
#endif
