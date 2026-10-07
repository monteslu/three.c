/* three.c <-> Box3D. Header-only, so libthree has no physics dependency.
 *
 * Give a body its object as userData, and after each b3World_Step call
 * t3_box3d_sync: it reads the step's body move events (only bodies that
 * moved) and copies each transform onto its object. Objects should be
 * children of the scene root, since the transform is world space.
 *
 *     b3BodyDef def = b3DefaultBodyDef();
 *     def.type = b3_dynamicBody;
 *     def.userData = mesh;
 *     t3_box3d_place(&def, mesh);   // body starts where the mesh is
 *     ...
 *     b3World_Step(world, 1.0f / 60, 4);
 *     t3_box3d_sync(world);
 */
#ifndef THREE_BOX3D_H
#define THREE_BOX3D_H

#include "box3d/box3d.h"
#include "three.h"

static inline void t3_box3d_apply(t3_object *o, b3WorldTransform xf) {
  o->position = t3_v3((float)xf.p.x, (float)xf.p.y, (float)xf.p.z);
  t3_quat q = { xf.q.v.x, xf.q.v.y, xf.q.v.z, xf.q.s };
  t3_object_set_quaternion(o, q);
}

/* Copy every moved body's transform to its userData object. */
static inline void t3_box3d_sync(b3WorldId world) {
  b3BodyEvents ev = b3World_GetBodyEvents(world);
  for (int i = 0; i < ev.moveCount; i++)
    if (ev.moveEvents[i].userData) t3_box3d_apply((t3_object *)ev.moveEvents[i].userData, ev.moveEvents[i].transform);
}

/* Start a body definition at an object's position and orientation. */
static inline void t3_box3d_place(b3BodyDef *def, const t3_object *o) {
  def->position.x = o->position.x;
  def->position.y = o->position.y;
  def->position.z = o->position.z;
  def->rotation.v.x = o->quaternion.x;
  def->rotation.v.y = o->quaternion.y;
  def->rotation.v.z = o->quaternion.z;
  def->rotation.s = o->quaternion.w;
}

#endif
