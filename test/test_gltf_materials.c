/* GLTFLoader material names, KHR_materials_* extensions and morph target
 * names (no GPU): a small glTF parsed from memory, its materials and meshes
 * checked against what r186's GLTFLoader builds from the same file.
 * T3_TEST_CONTROL=1 parses the same file with its extensions renamed (an
 * unknown extension is ignored): the checks must then fail. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "three.h"

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
#define NEAR(a, b) (fabsf((float)(a) - (float)(b)) < 1e-5f)

static const char *b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static void base64(const unsigned char *in, int n, char *out) {
  int o = 0;
  for (int i = 0; i < n; i += 3) {
    unsigned v = in[i] << 16 | (i + 1 < n ? in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
    out[o++] = b64[v >> 18 & 63];
    out[o++] = b64[v >> 12 & 63];
    out[o++] = i + 1 < n ? b64[v >> 6 & 63] : '=';
    out[o++] = i + 2 < n ? b64[v & 63] : '=';
  }
  out[o] = 0;
}

static t3_material *find_material(t3_object *o, const char *mesh_name) {
  t3_object *m = t3_object_get_by_name(o, mesh_name);
  return m && (m->type == T3_MESH || m->type == T3_SKINNED_MESH) ? ((t3_mesh *)m)->materials[0] : NULL;
}

int main(void) {
  bool control = getenv("T3_TEST_CONTROL") && *getenv("T3_TEST_CONTROL") == '1';
  /* positions (3 x vec3), morph target 0 and 1 (3 x vec3 each) */
  float data[27] = { 0, 0, 0, 1, 0, 0, 0, 1, 0,
                     0, 0, 0, 0, 0.5f, 0, 0, 0, 0,
                     0, 0, 0, 0, 0, 0, 0.25f, 0, 0 };
  char enc[200];
  base64((const unsigned char *)data, sizeof data, enc);
  const char *K = control ? "XKHR" : "KHR";
  char json[8192];
  snprintf(json, sizeof json,
    "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0,1,2,3,4]}],"
    "\"nodes\":[{\"name\":\"Face\",\"mesh\":0},{\"name\":\"Skin\",\"mesh\":1},{\"name\":\"Coat\",\"mesh\":2},"
    "{\"name\":\"Glass\",\"mesh\":3},{\"name\":\"Plain\",\"mesh\":4}],"
    "\"meshes\":["
    "{\"name\":\"FaceMesh\",\"extras\":{\"targetNames\":[\"smile\",\"blink\"]},\"weights\":[0.5,0],"
    "\"primitives\":[{\"attributes\":{\"POSITION\":0},\"targets\":[{\"POSITION\":1},{\"POSITION\":2}],\"material\":0}]},"
    "{\"name\":\"SkinMesh\",\"primitives\":[{\"attributes\":{\"POSITION\":0},\"material\":0}]},"
    "{\"name\":\"CoatMesh\",\"primitives\":[{\"attributes\":{\"POSITION\":0},\"material\":1}]},"
    "{\"name\":\"GlassMesh\",\"primitives\":[{\"attributes\":{\"POSITION\":0},\"material\":2}]},"
    "{\"name\":\"PlainMesh\",\"primitives\":[{\"attributes\":{\"POSITION\":0},\"material\":3}]}],"
    "\"materials\":["
    "{\"name\":\"Skin\",\"pbrMetallicRoughness\":{\"roughnessFactor\":0.6,\"metallicFactor\":0},"
    "\"extensions\":{\"%s_materials_sheen\":{\"sheenColorFactor\":[0.8,0.3,0.2],\"sheenRoughnessFactor\":0.7},"
    "\"%s_materials_specular\":{\"specularFactor\":0.4,\"specularColorFactor\":[1,0.9,0.8]},"
    "\"%s_materials_ior\":{\"ior\":1.4}}},"
    "{\"name\":\"Coat Paint\",\"emissiveFactor\":[1,0.5,0],"
    "\"extensions\":{\"%s_materials_clearcoat\":{\"clearcoatFactor\":0.9,\"clearcoatRoughnessFactor\":0.1},"
    "\"%s_materials_emissive_strength\":{\"emissiveStrength\":4}}},"
    "{\"name\":\"Glass\",\"extensions\":{\"%s_materials_transmission\":{\"transmissionFactor\":0.95},"
    "\"%s_materials_volume\":{\"thicknessFactor\":0.02,\"attenuationColor\":[0.9,1,0.95],\"attenuationDistance\":0.5}}},"
    "{\"name\":\"Plain\"}],"
    "\"buffers\":[{\"byteLength\":108,\"uri\":\"data:application/octet-stream;base64,%s\"}],"
    "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":36},"
    "{\"buffer\":0,\"byteOffset\":72,\"byteLength\":36}],"
    "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[1,1,0]},"
    "{\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[0,0.5,0]},"
    "{\"bufferView\":2,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[0.25,0,0]}]}",
    K, K, K, K, K, K, K, enc);
  t3_gltf *g = t3_gltf_parse(json, strlen(json), NULL, NULL);
  CHECK(g != NULL);
  if (!g) return 1;

  /* material names, kept on the material (and on the variant clones) */
  t3_material *skin = find_material(g->scene, "Skin");
  t3_material *coat = find_material(g->scene, "Coat");
  t3_material *glass = find_material(g->scene, "Glass");
  t3_material *plain = find_material(g->scene, "Plain");
  CHECK(skin && coat && glass && plain);
  if (!skin || !coat || !glass || !plain) return 1;
  CHECK(skin->name && !strcmp(skin->name, "Skin"));
  CHECK(coat->name && !strcmp(coat->name, "Coat Paint")); /* as written: material names are not sanitized */
  CHECK(plain->name && !strcmp(plain->name, "Plain"));
  t3_material *cl = t3_material_clone(skin);
  CHECK(cl->name == skin->name);
  t3_release(cl);
  /* a material named like a node does not rename the node */
  CHECK(t3_object_get_by_name(g->scene, "Skin") != NULL);
  CHECK(t3_object_get_by_name(g->scene, "Skin_1") == NULL);

  /* KHR_materials_sheen / specular / ior: a MeshPhysicalMaterial */
  CHECK(skin->type == T3_MESH_PHYSICAL_MATERIAL);
  CHECK(NEAR(skin->sheen, 1) && NEAR(skin->sheen_roughness, 0.7f));
  CHECK(NEAR(skin->sheen_color.r, 0.8f) && NEAR(skin->sheen_color.g, 0.3f) && NEAR(skin->sheen_color.b, 0.2f));
  CHECK(NEAR(skin->specular_intensity, 0.4f) && NEAR(skin->specular_color.b, 0.8f));
  CHECK(NEAR(skin->ior, 1.4f));
  CHECK(NEAR(skin->roughness, 0.6f) && NEAR(skin->metalness, 0));
  /* clearcoat + emissive strength */
  CHECK(coat->type == T3_MESH_PHYSICAL_MATERIAL);
  CHECK(NEAR(coat->clearcoat, 0.9f) && NEAR(coat->clearcoat_roughness, 0.1f));
  CHECK(NEAR(coat->emissive_intensity, 4) && NEAR(coat->emissive.g, 0.5f));
  /* transmission + volume */
  CHECK(glass->type == T3_MESH_PHYSICAL_MATERIAL);
  CHECK(NEAR(glass->transmission, 0.95f) && NEAR(glass->thickness, 0.02f));
  CHECK(NEAR(glass->attenuation_distance, 0.5f) && NEAR(glass->attenuation_color.g, 1));
  /* no extension: MeshStandardMaterial, defaults */
  CHECK(plain->type == T3_MESH_STANDARD_MATERIAL);
  CHECK(NEAR(plain->emissive_intensity, 1) && NEAR(plain->ior, 1.5f) && NEAR(plain->sheen, 0));

  /* morph targets: influences from the mesh weights, names from extras.targetNames */
  t3_mesh *face = (t3_mesh *)t3_object_get_by_name(g->scene, "Face");
  CHECK(face && face->morph_influence_count == 2);
  if (face && face->morph_influence_count == 2) {
    CHECK(NEAR(face->morph_influences[0], 0.5f));
    CHECK(t3_mesh_get_morph_target_index(face, "smile") == 0);
    CHECK(t3_mesh_get_morph_target_index(face, "blink") == 1);
    CHECK(t3_mesh_get_morph_target_index(face, "frown") == -1);
  }
  t3_mesh *sk = (t3_mesh *)t3_object_get_by_name(g->scene, "Skin");
  CHECK(sk && t3_mesh_get_morph_target_index(sk, "smile") == -1); /* no targets */

  t3_release(g);
  if (fails) { fprintf(stderr, "test_gltf_materials: %d failed%s\n", fails, control ? " (control)" : ""); return 1; }
  printf("test_gltf_materials: ok\n");
  return 0;
}
