/* three.c <-> Box2D v3. Header-only, so libthree has no physics dependency.
 *
 * Box2D's plane is three.js's x/y plane: a body's position sets the object's
 * x and y (z is left alone, so layers keep their depth) and its angle sets
 * rotation.z. Give each body its object as userData and call t3_box2d_sync
 * after b2World_Step; only bodies that moved are touched.
 */
#ifndef THREE_BOX2D_H
#define THREE_BOX2D_H

#include <math.h>

#include "box2d/box2d.h"
#include "three.h"

static inline void t3_box2d_apply(t3_object *o, b2Transform xf) {
  o->position.x = xf.p.x;
  o->position.y = xf.p.y;
  t3_object_set_rotation(o, o->rotation.x, o->rotation.y, atan2f(xf.q.s, xf.q.c));
}

static inline void t3_box2d_sync(b2WorldId world) {
  b2BodyEvents ev = b2World_GetBodyEvents(world);
  for (int i = 0; i < ev.moveCount; i++)
    if (ev.moveEvents[i].userData) t3_box2d_apply((t3_object *)ev.moveEvents[i].userData, ev.moveEvents[i].transform);
}

/* Start a body definition at an object's x, y and rotation.z. */
static inline void t3_box2d_place(b2BodyDef *def, const t3_object *o) {
  def->position.x = o->position.x;
  def->position.y = o->position.y;
  def->rotation = b2MakeRot(o->rotation.z);
}

#endif
