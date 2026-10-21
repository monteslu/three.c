/* Real instance upload path, mocked GL: changed/growing prefixes replace
 * storage, shrink/no-change does not upload, and matrices retain their data. */
#include <assert.h>
#include <string.h>
#include "../src/renderer.c"
#undef calloc
void *t3__calloc(size_t n,size_t size) { return calloc(n,size); }
void t3__fatal(const char *what) {(void)what;abort();}
static unsigned uploads,subuploads;
static GLsizeiptr uploaded;
static float gpu[64];
void glBindBuffer(GLenum target,GLuint buffer) {(void)target;(void)buffer;}
void glGenBuffers(GLsizei n,GLuint *buffers) {while(n--)*buffers++=1;}
void glBufferData(GLenum target,GLsizeiptr size,const void *data,GLenum usage) {
  (void)target;(void)usage;assert(size<=sizeof gpu);uploaded=size;uploads++;
  memcpy(gpu,data,(size_t)size);
}
void glBufferSubData(GLenum target,GLintptr offset,GLsizeiptr size,const void *data) {
  (void)target;(void)offset;(void)size;(void)data;subuploads++;
}
int main(void) {
  float data[64];for(int i=0;i<64;i++)data[i]=(float)i;
  attribute_gl ag={.buffer=1,.size=sizeof data,.version=1,.items_valid=4};
  t3_attribute a={.array=data,.count=4,.item_size=16,.version=2,.usage=T3_DYNAMIC_DRAW,._gl=&ag};
  instance_buffer(&a,1);assert(uploads==1&&uploaded==64&&ag.items_valid==1);
  instance_buffer(&a,3);assert(uploads==2&&uploaded==192&&gpu[47]==47);
  instance_buffer(&a,2);assert(uploads==2);
  data[0]=99;a.version++;instance_buffer(&a,2);assert(uploads==3&&uploaded==128&&gpu[0]==99);
  instance_buffer(&a,4);assert(uploads==4&&uploaded==256&&gpu[63]==63);
  instance_buffer(&a,0);assert(uploads==4&&subuploads==0);
  return 0;
}
