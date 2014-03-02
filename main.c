#include <sys/queue.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <netinet/in.h>

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <unistd.h>
#include "GLES2/gl2.h"
#include "EGL/egl.h"
#include "EGL/eglext.h"

#include "bcm_host.h"

#include "text.h"

#define check() assert(glGetError() == 0)

static uint32_t screen_width;
static uint32_t screen_height;
static EGLDisplay display;
static EGLSurface surface;
static EGLContext context;



typedef struct obj {
  GLuint buf;

  GLuint program;
  GLuint attr_vertex;
  GLuint attr_texcoord;
  GLuint modelview;
  GLuint projection;
  GLuint tex;

  int width;
  int height;

} obj_t;

static int dorun = 1;

TAILQ_HEAD(status_update_queue, status_update);

typedef struct status_update {
  TAILQ_ENTRY(status_update) link;
  uint8_t *bitmap;
  int width;
  int height;
} status_update_t;


static struct status_update_queue status_updates;
static pthread_mutex_t status_update_mutex = PTHREAD_MUTEX_INITIALIZER;

static int prog_tex;

const static float identity[] = {
  1,0,0,0,
  0,1,0,0,
  0,0,1,0,
  0,0,0,1
};


static void
init_gl(void)
{
   int32_t success = 0;
   EGLBoolean result;
   EGLint num_config;

   static EGL_DISPMANX_WINDOW_T nativewindow;

   DISPMANX_ELEMENT_HANDLE_T dispman_element;
   DISPMANX_DISPLAY_HANDLE_T dispman_display;
   DISPMANX_UPDATE_HANDLE_T dispman_update;
   VC_RECT_T dst_rect;
   VC_RECT_T src_rect;

   static const EGLint attribute_list[] =
   {
      EGL_RED_SIZE, 8,
      EGL_GREEN_SIZE, 8,
      EGL_BLUE_SIZE, 8,
      EGL_ALPHA_SIZE, 8,
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_NONE
   };
   
   static const EGLint context_attributes[] = 
   {
      EGL_CONTEXT_CLIENT_VERSION, 2,
      EGL_NONE
   };
   EGLConfig config;

   // get an EGL display connection
   display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   assert(display!=EGL_NO_DISPLAY);
   check();

   // initialize the EGL display connection
   result = eglInitialize(display, NULL, NULL);
   assert(EGL_FALSE != result);
   check();

   // get an appropriate EGL frame buffer configuration
   result = eglChooseConfig(display, attribute_list, &config, 1, &num_config);
   assert(EGL_FALSE != result);
   check();

   // get an appropriate EGL frame buffer configuration
   result = eglBindAPI(EGL_OPENGL_ES_API);
   assert(EGL_FALSE != result);
   check();

   // create an EGL rendering context
   context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);
   assert(context!=EGL_NO_CONTEXT);
   check();

   // create an EGL window surface
   success = graphics_get_display_size(0 /* LCD */, &screen_width, &screen_height);
   assert( success >= 0 );

   dst_rect.x = 0;
   dst_rect.y = 0;
   dst_rect.width = screen_width;
   dst_rect.height = screen_height;
      
   src_rect.x = 0;
   src_rect.y = 0;
   src_rect.width = screen_width << 16;
   src_rect.height = screen_height << 16;        

   dispman_display = vc_dispmanx_display_open( 0 /* LCD */);
   dispman_update = vc_dispmanx_update_start( 0 );
         
   dispman_element = vc_dispmanx_element_add(dispman_update, dispman_display,
      0/*layer*/, &dst_rect, 0/*src*/,
      &src_rect, DISPMANX_PROTECTION_NONE, 0 /*alpha*/, 0/*clamp*/, 0/*transform*/);
      
   nativewindow.element = dispman_element;
   nativewindow.width = screen_width;
   nativewindow.height = screen_height;
   vc_dispmanx_update_submit_sync( dispman_update );
      
   check();

   surface = eglCreateWindowSurface( display, config, &nativewindow, NULL );
   assert(surface != EGL_NO_SURFACE);
   check();

   // connect the context to the surface
   result = eglMakeCurrent(display, surface, surface, context);
   assert(EGL_FALSE != result);
   check();

   // Set background color and clear buffers
   glClearColor(0,0,0,0);
   glClear( GL_COLOR_BUFFER_BIT );

   check();
}


static void
compile_shader(int shader, const char *program)
{
  char log[4096];
  GLint len, v;
  
  glShaderSource(shader, 1, &program, 0);
  check();
  glCompileShader(shader);
  check();
  glGetShaderInfoLog(shader, sizeof(log), &len, log);
  check();
  glGetShaderiv(shader, GL_COMPILE_STATUS, &v);
  check();

  if(!v) {
    fprintf(stderr, "Unable to compile shader\n%s\n", log);
    exit(1);
  }
  glGetError();
}

/**
 *
 */
static void
clear_obj(obj_t *o)
{
  if(o->tex) {
    glDeleteTextures(1, &o->tex);
    o->tex = 0;
  }

  if(o->buf) {
    glDeleteBuffers(1, &o->buf);
    o->buf = 0;
  }
}

/**
 *
 */
static int
init_program(const char *vshader, const char *fshader)
{
  int vs = glCreateShader(GL_VERTEX_SHADER);
  compile_shader(vs, vshader);

  int fs = glCreateShader(GL_FRAGMENT_SHADER);
  compile_shader(fs, fshader);

  int p = glCreateProgram();
  glAttachShader(p, vs);
  glAttachShader(p, fs);
  glLinkProgram(p);
  check();
  return p;
 
}

/**
 *
 */
static void
init_obj(obj_t *o, float *vbuf, int vbuf_len, int program,
	 uint8_t *texture, int format, int tex_width, int tex_height,
	 float *projection)
{
  o->program = program;

  glUseProgram(o->program);
  check();

  o->attr_vertex = glGetAttribLocation(o->program, "vertex");
  check();

  o->modelview = glGetUniformLocation(o->program, "modelview");
  check();

  o->projection = glGetUniformLocation(o->program, "projection");
  check();


  glUniformMatrix4fv(o->projection, 1, 0, projection);
  check();

  glGenBuffers(1, &o->buf);
  check();
  
  glBindBuffer(GL_ARRAY_BUFFER, o->buf);
  check();
  glBufferData(GL_ARRAY_BUFFER, vbuf_len * sizeof(float), vbuf,
	       GL_STATIC_DRAW);
  check();

  glVertexAttribPointer(o->attr_vertex, 4, GL_FLOAT, 0, 6*4, 0);
  glEnableVertexAttribArray(o->attr_vertex);




  if(texture != NULL) {

    o->width  = tex_width;
    o->height = tex_height;

    o->attr_texcoord = glGetAttribLocation(o->program, "texcoord");
    check();

    glVertexAttribPointer(o->attr_texcoord, 2, GL_FLOAT, 0, 6*4, (void *)16);
    check();
    glEnableVertexAttribArray(o->attr_texcoord);
    check();

    int x = glGetUniformLocation(o->program, "tex0");
    check();
    glUniform1i(x, 0);
    check();

    glGenTextures(1, &o->tex);
    check();
    glBindTexture(GL_TEXTURE_2D, o->tex);
    check();
    glTexImage2D(GL_TEXTURE_2D, 0, format, tex_width, tex_height,
		 0, format, GL_UNSIGNED_BYTE, texture);
    check();
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    check();
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    check();
  }
}


/**
 *
 */
static void
rotate(float *dst, const float *o, float a, float x, float y, float z)
{
  if(o == NULL)
    o = identity;

  float s = sinf(a);
  float c = cosf(a);
  float t = 1.0 - c;
  float n = 1 / sqrtf(x*x + y*y + z*z);
  float m[16], p[16];

  x *= n;
  y *= n;
  z *= n;
  
  m[ 0] = t * x * x + c;
  m[ 4] = t * x * y - s * z;
  m[ 8] = t * x * z + s * y;
  m[12] = 0;

  m[ 1] = t * y * x + s * z;
  m[ 5] = t * y * y + c;
  m[ 9] = t * y * z - s * x;
  m[13] = 0;

  m[ 2] = t * z * x - s * y;
  m[ 6] = t * z * y + s * x;
  m[10] = t * z * z + c;
  m[14] = 0;

  p[0]  = o[0]*m[0]  + o[4]*m[1]  + o[8]*m[2];
  p[4]  = o[0]*m[4]  + o[4]*m[5]  + o[8]*m[6];
  p[8]  = o[0]*m[8]  + o[4]*m[9]  + o[8]*m[10];
  p[12] = o[0]*m[12] + o[4]*m[13] + o[8]*m[14] + o[12];
 
  p[1]  = o[1]*m[0]  + o[5]*m[1]  + o[9]*m[2];
  p[5]  = o[1]*m[4]  + o[5]*m[5]  + o[9]*m[6];
  p[9]  = o[1]*m[8]  + o[5]*m[9]  + o[9]*m[10];
  p[13] = o[1]*m[12] + o[5]*m[13] + o[9]*m[14] + o[13];
  
  p[2]  = o[2]*m[0]  + o[6]*m[1]  + o[10]*m[2];
  p[6]  = o[2]*m[4]  + o[6]*m[5]  + o[10]*m[6];
  p[10] = o[2]*m[8]  + o[6]*m[9]  + o[10]*m[10];
  p[14] = o[2]*m[12] + o[6]*m[13] + o[10]*m[14] + o[14];

  p[ 3] = 0;
  p[ 7] = 0;
  p[11] = 0;
  p[15] = 1;

  memcpy(dst, p, sizeof(float) * 16);

}


static void
translate(float *m, const float *src, float x, float y, float z)
{
  if(src == NULL)
    src = identity;
  memcpy(m, src, sizeof(float) * 16);

  m[12] += m[0]*x + m[4]*y +  m[8]*z;
  m[13] += m[1]*x + m[5]*y +  m[9]*z;
  m[14] += m[2]*x + m[6]*y + m[10]*z;
}


static void
ortho(float *m, int left, int right, int bottom, int top, int near, int far)
{
   float x_orth = 2.0f / (right - left);
   float y_orth = 2.0f / (top - bottom);
   float z_orth = -2.0f / (far - near);

   float tx = -(right + left) / (right - left);
   float ty = -(top + bottom) / (top - bottom);
   float tz = -(far + near) / (far - near);

   memset(m, 0, sizeof(float) * 16);

   m[0] = x_orth;
   m[5] = y_orth;
   m[10] = z_orth;
   m[12] = tx;
   m[13] = ty;
   m[14] = tz;
   m[15] = 1;
}


static float deg;

float zpos = -1;

static void
printmatrix(float *m)
{
  printf(
	 "%3.3f %3.3f %3.3f %3.3f\n"
	 "%3.3f %3.3f %3.3f %3.3f\n"
	 "%3.3f %3.3f %3.3f %3.3f\n"
	 "%3.3f %3.3f %3.3f %3.3f\n",
	 m[0],  m[1],  m[2],  m[3],
	 m[4],  m[5],  m[6],  m[7],
	 m[8],  m[9],  m[10], m[11],
	 m[12], m[13], m[14], m[15]);
	 
}


static float projection_2d[16];

const static float projection_persp[16] = {
  2.414213,0.000000,0.000000,0.000000,
  0.000000,2.414213,0.000000,0.000000,
  0.000000,0.000000,1.033898,-1.000000,
  0.000000,0.000000,2.033898,0.000000
};


/**
 *
 */
static void
draw_object(obj_t *o, float *mv)
{
  glBindBuffer(GL_ARRAY_BUFFER, o->buf);
  check();

  if(o->tex) {
    glBindTexture(GL_TEXTURE_2D, o->tex);
    check();
  }

  glUseProgram(o->program);
  check();
  glUniformMatrix4fv(o->modelview, 1, 0, mv);
  check();
  glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
  check();
}


/**
 *
 */
static void
draw_object_2d(obj_t *o, int x, int y, int centered)
{
  float mv[16];
  if(centered) {
    x -= o->width / 2;
    y -= o->height / 2;
  }

  translate(mv, NULL, x, y, 0);
  draw_object(o, mv);
}



static obj_t status_obj;



/**
 *
 */
static void
refresh_status_obj(void)
{
  status_update_t *su;
  pthread_mutex_lock(&status_update_mutex);
  su = TAILQ_FIRST(&status_updates);
  if(su != NULL)
    TAILQ_REMOVE(&status_updates, su, link);
  pthread_mutex_unlock(&status_update_mutex);

  if(su == NULL)
    return;

  clear_obj(&status_obj);

  float vertices[] = {
    0, 0, 0, 1, 0, 1,
    0, 0, 0, 1, 1, 1,
    0, 0, 0, 1, 1, 0,
    0, 0, 0, 1, 0, 0,
  };


  vertices[0 * 6 + 0] = 0;
  vertices[0 * 6 + 1] = su->height;

  vertices[1 * 6 + 0] = su->width;
  vertices[1 * 6 + 1] = su->height;

  vertices[2 * 6 + 0] = su->width;
  vertices[2 * 6 + 1] = 0;

  vertices[3 * 6 + 0] = 0;
  vertices[3 * 6 + 1] = 0;

  init_obj(&status_obj, vertices, 4*6, prog_tex,
	   su->bitmap, GL_LUMINANCE, su->width, su->height, projection_2d);

  free(su->bitmap);
  free(su);
}


/**
 *
 */
static void
mainloop(void)
{
  refresh_status_obj();


  glBindFramebuffer(GL_FRAMEBUFFER,0);
  glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
  check();

  if(status_obj.buf)
    draw_object_2d(&status_obj, screen_width / 2, screen_height / 2, 1);

  glFlush();
  glFinish();
  check();
        
  eglSwapBuffers(display, surface);
  check();
}


#define STR(A)  #A

const char *test_vshader =
  STR(
      attribute vec4 vertex;
      attribute vec2 texcoord;

      uniform mat4 projection;
      uniform mat4 modelview;
      varying vec2 uv;

      
      void main(void) {
	gl_Position = projection * modelview * vertex;
	uv = texcoord;
      }
      );

const char *test_fshader =
  STR(
      void main(void) {
	gl_FragColor = vec4(1,1,0,1);
      }
      );

const char *tex_fshader =
  STR(
      uniform sampler2D tex0;
      varying vec2 uv;

      void main(void) {
	gl_FragColor =  texture2D(tex0, uv);
      }
      );




/**
 *
 */
static void
enq_status(const char *str, int len)
{
  if(len == -1)
    len = strlen(str);

  status_update_t *su = malloc(sizeof(status_update_t));
  su->bitmap = text_render(str, len, &su->width, &su->height);
  pthread_mutex_lock(&status_update_mutex);
  TAILQ_INSERT_TAIL(&status_updates, su, link);
  pthread_mutex_unlock(&status_update_mutex);
}


static int udpport = 4004;

/**
 * Status input thread
 */
static void *
status_thread(void *aux)
{
  struct sockaddr_in sin;
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  char buf[2000];

  memset(&sin, 0, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_port = htons(udpport);

  if(bind(fd, (struct sockaddr *)&sin, sizeof(sin))) {
    perror("bind");
    return NULL;
  }

  while(1) {
    int n = read(fd, buf, 2000);
    if(n < 1)
      continue;
    while(n > 0 && buf[n-1] < 32) {
      n--;
      buf[n] = 0;
    }
    enq_status(buf, n);
  }
}


static void
doexit(int x)
{
  dorun = 0;
}

/**
 *
 */
int
main(int argc, char **argv)
{
  int opt;
  const char *font = NULL;
  const char *initial_str;
  while((opt = getopt(argc, argv, "p:f:s:")) != -1) {
    switch(opt) {
    case 'p':
      udpport = atoi(optarg);
      break;
    case 'f':
      font = optarg;
      break;
    case 's':
      initial_str = optarg;
      break;
    }
  }

  if(font == NULL)
    exit(1);

  TAILQ_INIT(&status_updates);

  text_init(font);

  obj_t test;

  bcm_host_init();
  init_gl();

  prog_tex = init_program(test_vshader, tex_fshader);

  ortho(projection_2d, 0, screen_width, screen_height, 0, -10, 10);

  printmatrix(projection_2d);

  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glDisable(GL_CULL_FACE);

  if(initial_str != NULL)
    enq_status(initial_str, -1);

  glViewport(0, 0, screen_width, screen_height);

  pthread_t tid;
  pthread_create(&tid, NULL, status_thread, NULL);

  FILE *fp = fopen("/var/run/stos-splash.pid", "w");
  fprintf(fp, "%d\n", (int)getpid());
  fclose(fp);

  signal(SIGTERM, doexit);
  signal(SIGINT, doexit);

  while(dorun) {
    mainloop();
  }

  unlink("/var/run/stos-splash.pid");
  printf("Stopped\n");
  exit(0);
}
