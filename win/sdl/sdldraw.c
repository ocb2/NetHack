#include <curses.h>
#include <GL/glew.h>
#include <limits.h>
#include <pthread.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_opengl.h>
#include <SDL2/SDL_ttf.h>
#include <stdbool.h>
#include <unistd.h>

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#define NK_SDL_GL3_IMPLEMENTATION

#define MAX_VERTEX_MEMORY 512 * 1024
#define MAX_ELEMENT_MEMORY 128 * 1024
#include "nuklear.h"
#include "nuklear_sdl_gl3.h"

#include "botl.h"
#include "hack.h"

extern short glyph2tile[];

#include "winsdl.h"

struct sdl_nk_media {
  GLint skin;
  struct nk_image menu;
  struct nk_image check;
  struct nk_image check_cursor;
  struct nk_image option;
  struct nk_image option_cursor;
  struct nk_image header;
  struct nk_image window;
  struct nk_image scrollbar_inc_button;
  struct nk_image scrollbar_inc_button_hover;
  struct nk_image scrollbar_dec_button;
  struct nk_image scrollbar_dec_button_hover;
  struct nk_image button;
  struct nk_image button_hover;
  struct nk_image button_active;
  struct nk_image tab_minimize;
  struct nk_image tab_maximize;
  struct nk_image slider;
  struct nk_image slider_hover;
  struct nk_image slider_active;
};

SDL_Window *sdl_window;
SDL_GLContext *sdl_context;
TTF_Font *sdl_font;
SDL_GameController *sdl_controller;
SDL_Joystick *sdl_joystick;
int sdl_joystick_id;

bool sdl_redraw_ = true;

// 0 to mean "tile not set", so these indices are offset by 1
//GLuint sdl_map[NH_SDL_MAP_HEIGHT][NH_SDL_MAP_WIDTH] = {0};

// 1 unit should be equal to one tile
// model matrix
GLfloat sdl_gl_mat_model[] = {
  1.0f, 0.0f, 0.0f, 0.0f,
  0.0f, 1.0f, 0.0f, 0.0f,
  0.0f, 0.0f, 1.0f, 0.0f,
  0.0f, 0.0f, 0.0f, 1.0f
};

// view matrix
GLfloat sdl_gl_mat_view[] = {
  1.0f, 0.0f, 0.0f, 0.0f,
  0.0f, 1.0f, 0.0f, 0.0f,
  0.0f, 0.0f, 1.0f, 0.0f,
  0.0f, 0.0f, 0.0f, 1.0f
};

// projection matrix
GLfloat sdl_gl_mat_proj[] = {
  1.0f, 0.0f, 0.0f,  0.0f,
  0.0f, 1.0f, 0.0f,  0.0f,
  0.0f, 0.0f, -1.0f, 0.0f,
  0.0f, 0.0f, 0.0f,  1.0f
};

// pos x, pos y, tex x, tex y
GLfloat sdl_gl_hud_points[] = {
  -1.0,  1.0, 0.0, 0.0,
  -1.0, -1.0, 0.0, 1.0,
  1.0, -1.0, 1.0, 1.0,
  -1.0,  1.0, 0.0, 0.0,
  1.0,  1.0, 1.0, 0.0,
  1.0, -1.0, 1.0, 1.0
};

GLuint sdl_gl_shader_hud;
GLuint sdl_gl_shader_map;

GLuint sdl_gl_tex_map;
GLuint sdl_gl_tex_hud;

GLuint sdl_gl_vbo_hud;
GLuint sdl_gl_vbo_map;

GLuint sdl_gl_vao_hud;
GLuint sdl_gl_vao_map;

GLuint sdl_gl_attr_map_tile;
GLuint sdl_gl_attr_hud_pos;
GLuint sdl_gl_attr_hud_tex;

GLuint sdl_gl_mat_model_loc;
GLuint sdl_gl_mat_view_loc;
GLuint sdl_gl_mat_proj_loc;

GLuint sdl_gl_hud_proj_loc;

// map display offset
GLuint sdl_gl_map_x = 0;
GLuint sdl_gl_map_y = 0;

// window width/height
int sdl_gl_win_w = 0;
int sdl_gl_win_h = 0;

// nuklear context
struct nk_context *sdl_nk_ctx;

// cursor location
int sdl_curs_x = 0;
int sdl_curs_y = 0;

// mouse button up (false) or down (true)
bool sdl_curs_state = false;

// shaders
// map shader
const GLchar* sdl_gl_map_vert_src = R"glsl(
  #version 150 core

  // This is actually an integer in CPU memory, GLSL converts it automatically
  // to a float, I don't really know why
  in float tile;

  flat out uint tile_v;

  void main() {
    tile_v = uint(tile);

    uint x = uint(gl_VertexID % 79);
    uint y = uint(gl_VertexID / 79);

    gl_Position = vec4(x, y, 0.0, 1.0);
  }
)glsl";

const GLchar* sdl_gl_map_geom_src = R"glsl(
  #version 150 core

  layout(points) in;
  layout(triangle_strip, max_vertices = 6) out;

  uniform mat4 model;
  uniform mat4 view;
  uniform mat4 projection;

  // The input to the geometry shader is not a vertex but a primitive, which
  // can have multiple vertices, each with their own out attribute as specified
  // in the preceding vertex shader. Our inputs to the GS are only points
  // so this array will only ever have one element, however, the GLSL compiler
  // does not know this, so the input to the geometry shader always has to be
  // an array type of the type passed out by the vertex shader, even if we are
  // only working with points.
  flat in uint tile_v[];

  flat out uint tile_g;
  smooth out vec2 tex;

  vec4 project(vec4 pos) {
    return projection * view * model * pos;
  }

  void main() {
    /*  0     4
(1, 1) 3+-----+  (0, 1)
        |\    |
        | \   |
        |  \  |
        |   \ |
        |    \|
(1, 0)  +-----+5 (0, 0)
        1     2  */

    tile_g = tile_v[0];
    vec4 pos = gl_in[0].gl_Position;

    tex = vec2(1, 1);
    gl_Position = project(pos);
    EmitVertex();
    tex = vec2(1, 0);
    gl_Position = project(pos + vec4(0, -1, 0, 0));
    EmitVertex();
    tex = vec2(0, 0);
    gl_Position = project(pos + vec4(1, -1, 0, 0));
    EmitVertex();
    tex = vec2(1, 1);
    gl_Position = project(pos);
    EmitVertex();
    tex = vec2(0, 1);
    gl_Position = project(pos + vec4(1, 0, 0, 0));
    EmitVertex();
    tex = vec2(0, 0);
    gl_Position = project(pos + vec4(1, -1, 0, 0));
    EmitVertex();

    EndPrimitive();
  }
)glsl";

const GLchar* sdl_gl_map_frag_src = R"glsl(
  #version 150 core

  uniform sampler2D tilemap;

  flat in uint tile_g;
  smooth in vec2 tex;

  out vec4 color;

  /* coordinate spaces:
       quad space [0, 1] interpolated, in tex
       texture space [0, 1] which is 64x64 16px tiles
  so we need to go tile index [0, 2000] -> texture coordinates [0, 1] then normalize tex so its lower bound is the right tile
  */

  void main() {
    if (tile_g == uint(0)) {
      color = vec4(0.0, 0.0, 0.0, 1.0);
    } else {
      uint tile = tile_g - uint(1);

      uint x = tile % uint(64);
      uint y = tile / uint(64);

      vec2 base = vec2(x, y) / 64.0;
      vec2 coord = tex / 64.0;

      color = texture(tilemap, base + coord);
    }
  }
)glsl";

// HUD shaders
const GLchar* sdl_gl_hud_vert_src = R"glsl(
  #version 150 core

  uniform mat4 projection;

  in vec2 pos;
  in vec2 tex;

  smooth out vec2 tex_f;

  void main() {
    tex_f = tex;
    gl_Position = vec4(pos, 0.0, 1.0);
  }
)glsl";

const GLchar* sdl_gl_hud_frag_src = R"glsl(
  #version 150 core

  uniform sampler2D tex_sampl;

  smooth in vec2 tex_f;

  out vec4 color;

  void main() {
    color = texture(tex_sampl, tex_f);
  }
)glsl";

// miscellaneous functions

int sdl_motion_filter(void *userdata, SDL_Event *event) {
  if (event->type == SDL_MOUSEMOTION) {
    return 0;
  } else {
    return 1;
  }
}

// menu functions
struct nk_rect sdl_nk_tile(int idx) {
  struct nk_rect rect;

  rect.x = (float)(idx % NH_SDL_TILEMAP_WIDTH)*16;
  rect.y = floor((float)idx / NH_SDL_TILEMAP_HEIGHT)*16;
  rect.w = (float)NH_SDL_TILE_WIDTH;
  rect.h = (float)NH_SDL_TILE_HEIGHT;

  return rect;
}

// TODO: NULL ques
bool sdl_nk_yn(struct sdl_nk_yn *yn) {
  bool term = false;

  int i;
  char c;
  int w = floor(sdl_gl_win_w * NH_SDL_UI_SCALE_W);
  int h = floor(sdl_gl_win_h * NH_SDL_UI_SCALE_H);
  int x = floor((sdl_gl_win_w - w) / 2.0);
  int y = floor((sdl_gl_win_h - h) / 2.0);

#ifdef DEBUG
  assert(yn->ques);
#endif

  if (nk_begin(sdl_nk_ctx,
               "yn",
               nk_rect(x, y, w, h),
               NK_WINDOW_BORDER)) {
    nk_layout_row_dynamic(sdl_nk_ctx, 80, 1);
    nk_label_wrap(sdl_nk_ctx, yn->ques);
    while (c = yn->choices[i++]) {
      char button[2];
      button[0] = c;
      button[1] = 0;
      if (nk_button_label(sdl_nk_ctx, &button)) {
        yn->ret = c;
        term = true;
        break;
      }
    } 
  }
  
  nk_end(sdl_nk_ctx);

  return term;
}

bool sdl_nk_menu(struct sdl_nk_menu *arg) {
  bool term = false;

  int list_len = sdl_state.window[arg->window].len;
  struct sdl_list *list = sdl_state.window[arg->window].entries;
  struct nk_list_view nk_list;

  int w = floor(sdl_gl_win_w * NH_SDL_UI_SCALE_W);
  int h = floor(sdl_gl_win_h * NH_SDL_UI_SCALE_H);
  int x = floor((sdl_gl_win_w - w) / 2.0);
  int y = floor((sdl_gl_win_h - h) / 2.0);

  int i = 0;

  if (nk_begin(sdl_nk_ctx,
               "menu",
               nk_rect(x, y, w, h),
               NK_WINDOW_BORDER)) {
    nk_layout_row_dynamic(sdl_nk_ctx, 800, 1);
    nk_list_view_begin(sdl_nk_ctx, &nk_list, "menu list", 0, 30, list_len);
    while (list) {
      struct sdl_win_entry *entry = (struct sdl_win_entry*)list->data;
      nk_layout_row_dynamic(sdl_nk_ctx, 30, 1);
      if (entry->glyph != NO_GLYPH) {
        struct nk_rect rect = sdl_nk_tile(glyph2tile[entry->glyph]);
        entry->selected = nk_select_image_label(sdl_nk_ctx, nk_subimage_id(sdl_gl_tex_map, NH_SDL_TILEMAP_X, NH_SDL_TILEMAP_Y, rect), entry->str, NK_TEXT_RIGHT, entry->selected ? 1 : 0) ? true : false;
      } else {
        entry->selected = nk_select_label(sdl_nk_ctx, entry->str, NK_TEXT_LEFT, entry->selected ? 1 : 0) ? true : false;
      }
      i++;
      list = list->next;
    }
    nk_list_view_end(&nk_list);

    nk_layout_row_dynamic(sdl_nk_ctx, 30, 2);

    if (nk_button_label(sdl_nk_ctx, "Ok")) {
      term = true;
    }
    if (nk_button_label(sdl_nk_ctx, "Cancel")) {
      term = true;
    }
  }
  
  nk_end(sdl_nk_ctx);

  return term;
}

void sdl_nk_text() {

}

// TODO: use bitmasks for this
inline int sdl_input_controller(SDL_Event *event) {
  printf("in input, button=%d, state=%d\n", event->cbutton.button, event->cbutton.state);
  int u = SDL_GameControllerGetButton(sdl_controller, SDL_CONTROLLER_BUTTON_DPAD_UP);
  int d = SDL_GameControllerGetButton(sdl_controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
  int l = SDL_GameControllerGetButton(sdl_controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
  int r = SDL_GameControllerGetButton(sdl_controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);

  // 
  if (u && l || u && r || d && l || d && r) {
    SDL_FlushEvent(SDL_CONTROLLERBUTTONDOWN);
  }

  if (u && l) { return 'y'; }
  else if (u && r) {return 'u';}
  else if (d && l) {return 'b';}
  else if (d && r) {return 'n';}
  else if (u) {return 'k';} 
  else if (d) {return 'j';} 
  else if (l) {return 'h';} 
  else if (r) {return 'l';}
  else {
    fprintf(stderr,"what\n");
    return 0;
  }
}

// OpenGL drawing functions
#ifdef DEBUG
const char *sdl_gl_debug_source(GLenum source) {
  switch (source) {
  case GL_DEBUG_SOURCE_API:
    return "API";
  case GL_DEBUG_SOURCE_WINDOW_SYSTEM:
    return "WINDOW_SYSTEM";
  case GL_DEBUG_SOURCE_SHADER_COMPILER:
    return "SHADER_COMPILER";
  case GL_DEBUG_SOURCE_THIRD_PARTY:
    return "THIRD_PARTY";
  case GL_DEBUG_SOURCE_APPLICATION:
    return "SOURCE_APPLICATION";
  case GL_DEBUG_SOURCE_OTHER:
  default:
    return "OTHER";
  }
}

const char *sdl_gl_debug_type(GLenum type) {
  switch (type) {
  case GL_DEBUG_TYPE_ERROR:
    return "ERROR";
  case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
    return "DEPRECATED_BEHAVIOR";
  case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
    return "UNDEFINED_BEHAVIOR";
  case GL_DEBUG_TYPE_PORTABILITY:
    return "PORTABILITY";
  case GL_DEBUG_TYPE_PERFORMANCE:
    return "PERFORMANCE";
  case GL_DEBUG_TYPE_OTHER:
  default:
    return "OTHER";
  }
}

const char *sdl_gl_debug_severity(GLenum severity) {
  switch (severity){
  case GL_DEBUG_SEVERITY_LOW:
    return "LOW";
  case GL_DEBUG_SEVERITY_MEDIUM:
    return "MEDIUM";
  case GL_DEBUG_SEVERITY_HIGH:
    return "HIGH";
  default:
    return "OTHER";
  }
}

// adapted from https://blog.nobel-joergensen.com/2013/02/17/debugging-opengl-part-2-using-gldebugmessagecallback/
void sdl_gl_debug(GLenum source,
                  GLenum type,
                  GLuint id,
                  GLenum severity,
                  GLsizei length,
                  const GLchar* message,
                  const void* userParam) {
  fprintf(stderr,
          "SDL: GL: %s: %s: severity: %s, id: %d: %s\n",
          sdl_gl_debug_source(source),
          sdl_gl_debug_type(type),
          sdl_gl_debug_severity(severity),
          id,
          message);

  if (severity == GL_DEBUG_SEVERITY_HIGH) {
    exit(NH_SDL_ERROR_GL);
  }
}
#endif

// http://www.songho.ca/opengl/gl_projectionmatrix.html
void sdl_gl_project(GLfloat r,
                    GLfloat l,
                    GLfloat t,
                    GLfloat b,
                    GLfloat f,
                    GLfloat n) {
  sdl_gl_mat_proj[0] = 2/(r-l);
  sdl_gl_mat_proj[5] = 2/(t-b);
  sdl_gl_mat_proj[10] = -2/(f-n);
  sdl_gl_mat_proj[12] = -(r+l)/(r-l);
  sdl_gl_mat_proj[13] = -(t+b)/(t-b);
  sdl_gl_mat_proj[14] = -(f+n)/(f-n);
}

void sdl_gl_zoom(GLfloat z) {
  if (z > 0) {
    sdl_gl_mat_view[0] *= z;
    sdl_gl_mat_view[5] *= z;
  } else {
    sdl_gl_mat_view[0] /= (-1)*z;
    sdl_gl_mat_view[5] /= (-1)*z;
  }
}

void sdl_gl_resize(int w, int h) {
  sdl_gl_win_w = w;
  sdl_gl_win_h = h;

  sdl_redraw();
}

void sdl_gl_center(int x, int y) {
  sdl_gl_mat_view[12] = -x * sdl_gl_mat_view[0];
  sdl_gl_mat_view[13] = -y * sdl_gl_mat_view[5];
}

GLuint sdl_gl_texture(SDL_Surface *surface) {
  assert(surface);

  GLint mode;
  GLuint tex;
  // in-memory format of surface could be anything, so here we blit it to a
  // surface of a specific format
  SDL_Surface* surface_rgb = SDL_CreateRGBSurface(0,
                                                  surface->w,
                                                  surface->h,
                                                  32,
                                                  0x000000FF,
                                                  0x0000FF00,
                                                  0x00FF0000,
                                                  0xFF000000);
  assert(surface_rgb);
  SDL_BlitSurface(surface, 0, surface_rgb, 0);

  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D,
               0,
               GL_RGBA,
               surface_rgb->w,
               surface_rgb->h,
               0,
               GL_RGBA,
               GL_UNSIGNED_BYTE,
               surface_rgb->pixels);
  glGenerateMipmap(GL_TEXTURE_2D);

  SDL_FreeSurface(surface);
  SDL_FreeSurface(surface_rgb);

  return tex;
}

GLuint sdl_gl_shader(GLenum type, const GLchar* src) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &src, NULL);
  glCompileShader(shader);
  GLint shader_success = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &shader_success);

  // https://www.khronos.org/opengl/wiki/Shader_Compilation#Shader_error_handling
  if (shader_success == GL_FALSE) {
    printf("SDL: GL: shader compilation failure\n");

    GLint shader_err_len = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &shader_err_len);

    char *shader_err = malloc(shader_err_len);
    glGetShaderInfoLog(shader, shader_err_len, &shader_err_len, shader_err);

    write(STDERR_FILENO, shader_err, shader_err_len);

    fflush(stderr);
    free(shader_err);
    glDeleteShader(shader);
    exit(NH_SDL_ERROR_GL);
  }

  return shader;
}

GLint sdl_gl_program(const char* vert_src,
                     const char* geom_src,
                     const char* frag_src) {

  GLuint shader = glCreateProgram();
  glAttachShader(shader, sdl_gl_shader(GL_VERTEX_SHADER, vert_src));
  if (geom_src) {
    glAttachShader(shader, sdl_gl_shader(GL_GEOMETRY_SHADER, geom_src));
  }
  glAttachShader(shader, sdl_gl_shader(GL_FRAGMENT_SHADER, frag_src));
  glLinkProgram(shader);

  GLint shader_linked = 0;
  glGetProgramiv(shader, GL_LINK_STATUS, &shader_linked);
  if (shader_linked == GL_FALSE) {
    GLint shader_err_len = 0;
    glGetProgramiv(shader, GL_INFO_LOG_LENGTH, &shader_err_len);

    char *shader_err = malloc(shader_err_len);
    glGetShaderInfoLog(shader, shader_err_len, &shader_err_len, shader_err);

    write(STDERR_FILENO, shader_err, shader_err_len);

    fflush(stderr);
    free(shader_err);
    exit(NH_SDL_ERROR_GL);
  }

  return shader;
}

void sdl_redraw() {
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  sdl_gl_project(sdl_gl_win_w/2,
                 -sdl_gl_win_w/2,
                 -sdl_gl_win_h/2,
                 sdl_gl_win_h/2,
                 -1, 1);

  glViewport(0,
             0,
             sdl_gl_win_w,
             sdl_gl_win_h);

  glEnable(GL_BLEND);

  // draw map
  glUseProgram(sdl_gl_shader_map);
  glBindBuffer(GL_ARRAY_BUFFER, sdl_gl_vbo_map);
  glBindVertexArray(sdl_gl_vao_map);

  glBufferData(GL_ARRAY_BUFFER, sizeof(sdl_state.map), sdl_state.map, GL_STATIC_DRAW);

  glUniformMatrix4fv(sdl_gl_mat_model_loc, 1, GL_FALSE, sdl_gl_mat_model);
  glUniformMatrix4fv(sdl_gl_mat_view_loc, 1, GL_FALSE, sdl_gl_mat_view);
  glUniformMatrix4fv(sdl_gl_mat_proj_loc, 1, GL_FALSE, sdl_gl_mat_proj);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, sdl_gl_tex_map);

  glDrawArrays(GL_POINTS, 0, NH_SDL_MAP_HEIGHT * NH_SDL_MAP_WIDTH);

  // draw HUD
  glUseProgram(sdl_gl_shader_hud);
  glBindBuffer(GL_ARRAY_BUFFER, sdl_gl_vbo_hud);
  glBindVertexArray(sdl_gl_vao_hud);
  glBufferData(GL_ARRAY_BUFFER, sizeof(sdl_gl_hud_points), sdl_gl_hud_points, GL_STATIC_DRAW);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, sdl_gl_tex_hud);
  glUniformMatrix4fv(sdl_gl_hud_proj_loc, 1, GL_FALSE, sdl_gl_mat_proj);
  //glDrawArrays(GL_TRIANGLES, 0, 6);

  if (sdl_nk_func) {
    nk_sdl_render(NK_ANTI_ALIASING_OFF, MAX_VERTEX_MEMORY, MAX_ELEMENT_MEMORY);
  }
  
  SDL_GL_SwapWindow(sdl_window);
}

int sdl_init(void) {
  SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);

  if (SDL_NumJoysticks() > 0) {
    printf("in joysticks\n");
    sdl_controller = SDL_GameControllerOpen(0);
    sdl_joystick = SDL_GameControllerGetJoystick(sdl_controller);
    sdl_joystick_id = SDL_JoystickInstanceID(sdl_joystick);
    assert(sdl_controller);
    assert(sdl_joystick);
  } else if (SDL_NumJoysticks() < 0){
    printf("joystick init error\n");
    exit(-1);
  }

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);

  sdl_window = SDL_CreateWindow("nethack",
                                SDL_WINDOWPOS_UNDEFINED,
                                SDL_WINDOWPOS_UNDEFINED,
                                64 * 16,
                                64 * 16,
                                SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
  NTRY(sdl_window);

  sdl_context = SDL_GL_CreateContext(sdl_window);
  NTRY(sdl_context);
  SDL_GL_MakeCurrent(sdl_window, sdl_context);

  printf("dbg: %p %p\n", sdl_window, sdl_context);

  glewExperimental = GL_TRUE;
  glewInit();

#ifdef DEBUG
  glEnable(GL_DEBUG_OUTPUT);
  glDebugMessageCallback(sdl_gl_debug, NULL);
  glDebugMessageControl(GL_DONT_CARE,
                        GL_DONT_CARE,
                        GL_DONT_CARE,
                        0,
                        NULL,
                        GL_TRUE);
  // this message gets spammed constantly without this
  const GLuint debug_ids[] = {131185};
  glDebugMessageControl(GL_DEBUG_SOURCE_API,
                        GL_DEBUG_TYPE_OTHER,
                        GL_DONT_CARE,
                        1,
                        &debug_ids,
                        GL_FALSE);
#endif

  SDL_GL_SetSwapInterval(1);
  sdl_gl_zoom(10);
  glEnable (GL_BLEND); glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  sdl_gl_shader_map = sdl_gl_program(sdl_gl_map_vert_src,
                                     sdl_gl_map_geom_src,
                                     sdl_gl_map_frag_src);
  sdl_gl_shader_hud = sdl_gl_program(sdl_gl_hud_vert_src,
                                     NULL,
                                     sdl_gl_hud_frag_src);

  GLuint shader;
  shader = sdl_gl_shader_map;
  glUseProgram(shader);

  sdl_gl_mat_model_loc = glGetUniformLocation(shader, "model");
  sdl_gl_mat_view_loc = glGetUniformLocation(shader, "view");
  sdl_gl_mat_proj_loc = glGetUniformLocation(shader, "projection");
  sdl_gl_hud_proj_loc = glGetUniformLocation(sdl_gl_shader_hud, "projection");

  sdl_gl_tex_map = sdl_gl_texture(IMG_Load(NH_SDL_PATH_TILE));
  glUniform1i(glGetUniformLocation(shader, "tilemap"), 0);

  glGenBuffers(1, &sdl_gl_vbo_map);
  glBindBuffer(GL_ARRAY_BUFFER, sdl_gl_vbo_map);
  glBufferData(GL_ARRAY_BUFFER, sizeof(sdl_state.map), sdl_state.map, GL_STATIC_DRAW);

  glGenVertexArrays(1, &sdl_gl_vao_map);
  glBindVertexArray(sdl_gl_vao_map);
  
  sdl_gl_attr_map_tile = glGetAttribLocation(shader, "tile");
  glEnableVertexAttribArray(sdl_gl_attr_map_tile);
  glVertexAttribPointer(sdl_gl_attr_map_tile, 1, GL_UNSIGNED_INT, GL_FALSE, 0, 0);

  shader = sdl_gl_shader_hud;
  glUseProgram(shader);

  if (TTF_Init() < 0) {
    fprintf(stderr, "SDL: TTF: initialization error\n");
    exit(NH_SDL_ERROR_TTF);
  }

  sdl_font = TTF_OpenFont(NH_SDL_PATH_FONT, 16);
  NTRY(sdl_font);

  SDL_Color text_color = {0xFF, 0x00, 0x00};
  sdl_gl_tex_hud = sdl_gl_texture(TTF_RenderText_Solid(sdl_font, "hud text", text_color));
  glUniform1i(glGetUniformLocation(shader, "tex_sampl"), 1);

  glGenBuffers(1, &sdl_gl_vbo_hud);
  glBindBuffer(GL_ARRAY_BUFFER, sdl_gl_vbo_hud);
  glBufferData(GL_ARRAY_BUFFER, sizeof(sdl_gl_hud_points), sdl_gl_hud_points, GL_STATIC_DRAW);

  glGenVertexArrays(1, &sdl_gl_vao_hud);
  glBindVertexArray(sdl_gl_vao_hud);
  
  sdl_gl_attr_hud_pos = glGetAttribLocation(shader, "pos");
  glEnableVertexAttribArray(sdl_gl_attr_hud_pos);
  glVertexAttribPointer(sdl_gl_attr_hud_pos, 2, GL_FLOAT, GL_FALSE, 4*sizeof(GLfloat), 0);
  
  sdl_gl_attr_hud_tex = glGetAttribLocation(shader, "tex");
  glEnableVertexAttribArray(sdl_gl_attr_hud_tex);
  glVertexAttribPointer(sdl_gl_attr_hud_tex, 2, GL_FLOAT, GL_FALSE, 4*sizeof(GLfloat), (void*)(2*sizeof(GLfloat)));

  // mostly taken from the nuklear SDL demo
  sdl_nk_ctx = nk_sdl_init(sdl_window);
  struct nk_font_atlas *nk_atlas;
  nk_sdl_font_stash_begin(&nk_atlas);
  nk_sdl_font_stash_end();

  // copy pasted from nuklear's skinning.c file
  struct sdl_nk_media media;

  media.skin = sdl_gl_texture(IMG_Load(NH_SDL_PATH_SKIN));
  media.check = nk_subimage_id(media.skin, 512,512, nk_rect(464,32,15,15));
  media.check_cursor = nk_subimage_id(media.skin, 512,512, nk_rect(450,34,11,11));
  media.option = nk_subimage_id(media.skin, 512,512, nk_rect(464,64,15,15));
  media.option_cursor = nk_subimage_id(media.skin, 512,512, nk_rect(451,67,9,9));
  media.header = nk_subimage_id(media.skin, 512,512, nk_rect(128,0,127,24));
  media.window = nk_subimage_id(media.skin, 512,512, nk_rect(128,23,127,104));
  media.scrollbar_inc_button = nk_subimage_id(media.skin, 512,512, nk_rect(464,256,15,15));
  media.scrollbar_inc_button_hover = nk_subimage_id(media.skin, 512,512, nk_rect(464,320,15,15));
  media.scrollbar_dec_button = nk_subimage_id(media.skin, 512,512, nk_rect(464,224,15,15));
  media.scrollbar_dec_button_hover = nk_subimage_id(media.skin, 512,512, nk_rect(464,288,15,15));
  media.button = nk_subimage_id(media.skin, 512,512, nk_rect(384,336,127,31));
  media.button_hover = nk_subimage_id(media.skin, 512,512, nk_rect(384,368,127,31));
  media.button_active = nk_subimage_id(media.skin, 512,512, nk_rect(384,400,127,31));
  media.tab_minimize = nk_subimage_id(media.skin, 512,512, nk_rect(451, 99, 9, 9));
  media.tab_maximize = nk_subimage_id(media.skin, 512,512, nk_rect(467,99,9,9));
  media.slider = nk_subimage_id(media.skin, 512,512, nk_rect(418,33,11,14));
  media.slider_hover = nk_subimage_id(media.skin, 512,512, nk_rect(418,49,11,14));
  media.slider_active = nk_subimage_id(media.skin, 512,512, nk_rect(418,64,11,14));

  /* window */
  sdl_nk_ctx->style.window.background = nk_rgb(204,204,204);
  sdl_nk_ctx->style.window.fixed_background = nk_style_item_image(media.window);
  sdl_nk_ctx->style.window.border_color = nk_rgb(67,67,67);
  sdl_nk_ctx->style.window.combo_border_color = nk_rgb(67,67,67);
  sdl_nk_ctx->style.window.contextual_border_color = nk_rgb(67,67,67);
  sdl_nk_ctx->style.window.menu_border_color = nk_rgb(67,67,67);
  sdl_nk_ctx->style.window.group_border_color = nk_rgb(67,67,67);
  sdl_nk_ctx->style.window.tooltip_border_color = nk_rgb(67,67,67);
  sdl_nk_ctx->style.window.scrollbar_size = nk_vec2(16,16);
  sdl_nk_ctx->style.window.border_color = nk_rgba(0,0,0,0);
  sdl_nk_ctx->style.window.padding = nk_vec2(8,4);
  sdl_nk_ctx->style.window.border = 3;

  /* window header */
  sdl_nk_ctx->style.window.header.normal = nk_style_item_image(media.header);
  sdl_nk_ctx->style.window.header.hover = nk_style_item_image(media.header);
  sdl_nk_ctx->style.window.header.active = nk_style_item_image(media.header);
  sdl_nk_ctx->style.window.header.label_normal = nk_rgb(95,95,95);
  sdl_nk_ctx->style.window.header.label_hover = nk_rgb(95,95,95);
  sdl_nk_ctx->style.window.header.label_active = nk_rgb(95,95,95);

  /* scrollbar */
  sdl_nk_ctx->style.scrollv.normal          = nk_style_item_color(nk_rgb(184,184,184));
  sdl_nk_ctx->style.scrollv.hover           = nk_style_item_color(nk_rgb(184,184,184));
  sdl_nk_ctx->style.scrollv.active          = nk_style_item_color(nk_rgb(184,184,184));
  sdl_nk_ctx->style.scrollv.cursor_normal   = nk_style_item_color(nk_rgb(220,220,220));
  sdl_nk_ctx->style.scrollv.cursor_hover    = nk_style_item_color(nk_rgb(235,235,235));
  sdl_nk_ctx->style.scrollv.cursor_active   = nk_style_item_color(nk_rgb(99,202,255));
  sdl_nk_ctx->style.scrollv.dec_symbol      = NK_SYMBOL_NONE;
  sdl_nk_ctx->style.scrollv.inc_symbol      = NK_SYMBOL_NONE;
  sdl_nk_ctx->style.scrollv.show_buttons    = nk_true;
  sdl_nk_ctx->style.scrollv.border_color    = nk_rgb(81,81,81);
  sdl_nk_ctx->style.scrollv.cursor_border_color = nk_rgb(81,81,81);
  sdl_nk_ctx->style.scrollv.border          = 1;
  sdl_nk_ctx->style.scrollv.rounding        = 0;
  sdl_nk_ctx->style.scrollv.border_cursor   = 1;
  sdl_nk_ctx->style.scrollv.rounding_cursor = 2;

  /* scrollbar buttons */
  sdl_nk_ctx->style.scrollv.inc_button.normal          = nk_style_item_image(media.scrollbar_inc_button);
  sdl_nk_ctx->style.scrollv.inc_button.hover           = nk_style_item_image(media.scrollbar_inc_button_hover);
  sdl_nk_ctx->style.scrollv.inc_button.active          = nk_style_item_image(media.scrollbar_inc_button_hover);
  sdl_nk_ctx->style.scrollv.inc_button.border_color    = nk_rgba(0,0,0,0);
  sdl_nk_ctx->style.scrollv.inc_button.text_background = nk_rgba(0,0,0,0);
  sdl_nk_ctx->style.scrollv.inc_button.text_normal     = nk_rgba(0,0,0,0);
  sdl_nk_ctx->style.scrollv.inc_button.text_hover      = nk_rgba(0,0,0,0);
  sdl_nk_ctx->style.scrollv.inc_button.text_active     = nk_rgba(0,0,0,0);
  sdl_nk_ctx->style.scrollv.inc_button.border          = 0.0f;

  sdl_nk_ctx->style.scrollv.dec_button.normal          = nk_style_item_image(media.scrollbar_dec_button);
  sdl_nk_ctx->style.scrollv.dec_button.hover           = nk_style_item_image(media.scrollbar_dec_button_hover);
  sdl_nk_ctx->style.scrollv.dec_button.active          = nk_style_item_image(media.scrollbar_dec_button_hover);
  sdl_nk_ctx->style.scrollv.dec_button.border_color    = nk_rgba(0,0,0,0);
  sdl_nk_ctx->style.scrollv.dec_button.text_background = nk_rgba(0,0,0,0);
  sdl_nk_ctx->style.scrollv.dec_button.text_normal     = nk_rgba(0,0,0,0);
  sdl_nk_ctx->style.scrollv.dec_button.text_hover      = nk_rgba(0,0,0,0);
  sdl_nk_ctx->style.scrollv.dec_button.text_active     = nk_rgba(0,0,0,0);
  sdl_nk_ctx->style.scrollv.dec_button.border          = 0.0f;

  /* checkbox toggle */
  {struct nk_style_toggle *toggle;
    toggle = &sdl_nk_ctx->style.checkbox;
    toggle->normal          = nk_style_item_image(media.check);
    toggle->hover           = nk_style_item_image(media.check);
    toggle->active          = nk_style_item_image(media.check);
    toggle->cursor_normal   = nk_style_item_image(media.check_cursor);
    toggle->cursor_hover    = nk_style_item_image(media.check_cursor);
    toggle->text_normal     = nk_rgb(95,95,95);
    toggle->text_hover      = nk_rgb(95,95,95);
    toggle->text_active     = nk_rgb(95,95,95);}

  /* option toggle */
  {struct nk_style_toggle *toggle;
    toggle = &sdl_nk_ctx->style.option;
    toggle->normal          = nk_style_item_image(media.option);
    toggle->hover           = nk_style_item_image(media.option);
    toggle->active          = nk_style_item_image(media.option);
    toggle->cursor_normal   = nk_style_item_image(media.option_cursor);
    toggle->cursor_hover    = nk_style_item_image(media.option_cursor);
    toggle->text_normal     = nk_rgb(95,95,95);
    toggle->text_hover      = nk_rgb(95,95,95);
    toggle->text_active     = nk_rgb(95,95,95);}

  /* default button */
  sdl_nk_ctx->style.button.normal = nk_style_item_image(media.button);
  sdl_nk_ctx->style.button.hover = nk_style_item_image(media.button_hover);
  sdl_nk_ctx->style.button.active = nk_style_item_image(media.button_active);
  sdl_nk_ctx->style.button.border_color = nk_rgba(0,0,0,0);
  sdl_nk_ctx->style.button.text_background = nk_rgba(0,0,0,0);
  sdl_nk_ctx->style.button.text_normal = nk_rgb(95,95,95);
  sdl_nk_ctx->style.button.text_hover = nk_rgb(95,95,95);
  sdl_nk_ctx->style.button.text_active = nk_rgb(95,95,95);

  /* default text */
  sdl_nk_ctx->style.text.color = nk_rgb(95,95,95);

  /* contextual button */
  sdl_nk_ctx->style.contextual_button.normal = nk_style_item_color(nk_rgb(206,206,206));
  sdl_nk_ctx->style.contextual_button.hover = nk_style_item_color(nk_rgb(229,229,229));
  sdl_nk_ctx->style.contextual_button.active = nk_style_item_color(nk_rgb(99,202,255));
  sdl_nk_ctx->style.contextual_button.border_color = nk_rgba(0,0,0,0);
  sdl_nk_ctx->style.contextual_button.text_background = nk_rgba(0,0,0,0);
  sdl_nk_ctx->style.contextual_button.text_normal = nk_rgb(95,95,95);
  sdl_nk_ctx->style.contextual_button.text_hover = nk_rgb(95,95,95);
  sdl_nk_ctx->style.contextual_button.text_active = nk_rgb(95,95,95);

  /* menu button */
  sdl_nk_ctx->style.menu_button.normal = nk_style_item_color(nk_rgb(206,206,206));
  sdl_nk_ctx->style.menu_button.hover = nk_style_item_color(nk_rgb(229,229,229));
  sdl_nk_ctx->style.menu_button.active = nk_style_item_color(nk_rgb(99,202,255));
  sdl_nk_ctx->style.menu_button.border_color = nk_rgba(0,0,0,0);
  sdl_nk_ctx->style.menu_button.text_background = nk_rgba(0,0,0,0);
  sdl_nk_ctx->style.menu_button.text_normal = nk_rgb(95,95,95);
  sdl_nk_ctx->style.menu_button.text_hover = nk_rgb(95,95,95);
  sdl_nk_ctx->style.menu_button.text_active = nk_rgb(95,95,95);

  /* tree */
  sdl_nk_ctx->style.tab.text = nk_rgb(95,95,95);
  sdl_nk_ctx->style.tab.tab_minimize_button.normal = nk_style_item_image(media.tab_minimize);
  sdl_nk_ctx->style.tab.tab_minimize_button.hover = nk_style_item_image(media.tab_minimize);
  sdl_nk_ctx->style.tab.tab_minimize_button.active = nk_style_item_image(media.tab_minimize);
  sdl_nk_ctx->style.tab.tab_minimize_button.text_background = nk_rgba(0,0,0,0);
  sdl_nk_ctx->style.tab.tab_minimize_button.text_normal = nk_rgba(0,0,0,0);
  sdl_nk_ctx->style.tab.tab_minimize_button.text_hover = nk_rgba(0,0,0,0);
  sdl_nk_ctx->style.tab.tab_minimize_button.text_active = nk_rgba(0,0,0,0);

  sdl_nk_ctx->style.tab.tab_maximize_button.normal = nk_style_item_image(media.tab_maximize);
  sdl_nk_ctx->style.tab.tab_maximize_button.hover = nk_style_item_image(media.tab_maximize);
  sdl_nk_ctx->style.tab.tab_maximize_button.active = nk_style_item_image(media.tab_maximize);
  sdl_nk_ctx->style.tab.tab_maximize_button.text_background = nk_rgba(0,0,0,0);
  sdl_nk_ctx->style.tab.tab_maximize_button.text_normal = nk_rgba(0,0,0,0);
  sdl_nk_ctx->style.tab.tab_maximize_button.text_hover = nk_rgba(0,0,0,0);
  sdl_nk_ctx->style.tab.tab_maximize_button.text_active = nk_rgba(0,0,0,0);

  sdl_nk_ctx->style.tab.node_minimize_button.normal = nk_style_item_image(media.tab_minimize);
  sdl_nk_ctx->style.tab.node_minimize_button.hover = nk_style_item_image(media.tab_minimize);
  sdl_nk_ctx->style.tab.node_minimize_button.active = nk_style_item_image(media.tab_minimize);
  sdl_nk_ctx->style.tab.node_minimize_button.text_background = nk_rgba(0,0,0,0);
  sdl_nk_ctx->style.tab.node_minimize_button.text_normal = nk_rgba(0,0,0,0);
  sdl_nk_ctx->style.tab.node_minimize_button.text_hover = nk_rgba(0,0,0,0);
  sdl_nk_ctx->style.tab.node_minimize_button.text_active = nk_rgba(0,0,0,0);

  sdl_nk_ctx->style.tab.node_maximize_button.normal = nk_style_item_image(media.tab_maximize);
  sdl_nk_ctx->style.tab.node_maximize_button.hover = nk_style_item_image(media.tab_maximize);
  sdl_nk_ctx->style.tab.node_maximize_button.active = nk_style_item_image(media.tab_maximize);
  sdl_nk_ctx->style.tab.node_maximize_button.text_background = nk_rgba(0,0,0,0);
  sdl_nk_ctx->style.tab.node_maximize_button.text_normal = nk_rgba(0,0,0,0);
  sdl_nk_ctx->style.tab.node_maximize_button.text_hover = nk_rgba(0,0,0,0);
  sdl_nk_ctx->style.tab.node_maximize_button.text_active = nk_rgba(0,0,0,0);

  /* selectable */
  sdl_nk_ctx->style.selectable.normal = nk_style_item_color(nk_rgb(206,206,206));
  sdl_nk_ctx->style.selectable.hover = nk_style_item_color(nk_rgb(206,206,206));
  sdl_nk_ctx->style.selectable.pressed = nk_style_item_color(nk_rgb(206,206,206));
  sdl_nk_ctx->style.selectable.normal_active = nk_style_item_color(nk_rgb(185,205,248));
  sdl_nk_ctx->style.selectable.hover_active = nk_style_item_color(nk_rgb(185,205,248));
  sdl_nk_ctx->style.selectable.pressed_active = nk_style_item_color(nk_rgb(185,205,248));
  sdl_nk_ctx->style.selectable.text_normal = nk_rgb(95,95,95);
  sdl_nk_ctx->style.selectable.text_hover = nk_rgb(95,95,95);
  sdl_nk_ctx->style.selectable.text_pressed = nk_rgb(95,95,95);
  sdl_nk_ctx->style.selectable.text_normal_active = nk_rgb(95,95,95);
  sdl_nk_ctx->style.selectable.text_hover_active = nk_rgb(95,95,95);
  sdl_nk_ctx->style.selectable.text_pressed_active = nk_rgb(95,95,95);

  /* slider */
  sdl_nk_ctx->style.slider.normal          = nk_style_item_hide();
  sdl_nk_ctx->style.slider.hover           = nk_style_item_hide();
  sdl_nk_ctx->style.slider.active          = nk_style_item_hide();
  sdl_nk_ctx->style.slider.bar_normal      = nk_rgb(156,156,156);
  sdl_nk_ctx->style.slider.bar_hover       = nk_rgb(156,156,156);
  sdl_nk_ctx->style.slider.bar_active      = nk_rgb(156,156,156);
  sdl_nk_ctx->style.slider.bar_filled      = nk_rgb(156,156,156);
  sdl_nk_ctx->style.slider.cursor_normal   = nk_style_item_image(media.slider);
  sdl_nk_ctx->style.slider.cursor_hover    = nk_style_item_image(media.slider_hover);
  sdl_nk_ctx->style.slider.cursor_active   = nk_style_item_image(media.slider_active);
  sdl_nk_ctx->style.slider.cursor_size     = nk_vec2(16.5f,21);
  sdl_nk_ctx->style.slider.bar_height      = 1;

  /* progressbar */
  sdl_nk_ctx->style.progress.normal = nk_style_item_color(nk_rgb(231,231,231));
  sdl_nk_ctx->style.progress.hover = nk_style_item_color(nk_rgb(231,231,231));
  sdl_nk_ctx->style.progress.active = nk_style_item_color(nk_rgb(231,231,231));
  sdl_nk_ctx->style.progress.cursor_normal = nk_style_item_color(nk_rgb(63,242,93));
  sdl_nk_ctx->style.progress.cursor_hover = nk_style_item_color(nk_rgb(63,242,93));
  sdl_nk_ctx->style.progress.cursor_active = nk_style_item_color(nk_rgb(63,242,93));
  sdl_nk_ctx->style.progress.border_color = nk_rgb(114,116,115);
  sdl_nk_ctx->style.progress.padding = nk_vec2(0,0);
  sdl_nk_ctx->style.progress.border = 2;
  sdl_nk_ctx->style.progress.rounding = 1;

  /* combo */
  sdl_nk_ctx->style.combo.normal = nk_style_item_color(nk_rgb(216,216,216));
  sdl_nk_ctx->style.combo.hover = nk_style_item_color(nk_rgb(216,216,216));
  sdl_nk_ctx->style.combo.active = nk_style_item_color(nk_rgb(216,216,216));
  sdl_nk_ctx->style.combo.border_color = nk_rgb(95,95,95);
  sdl_nk_ctx->style.combo.label_normal = nk_rgb(95,95,95);
  sdl_nk_ctx->style.combo.label_hover = nk_rgb(95,95,95);
  sdl_nk_ctx->style.combo.label_active = nk_rgb(95,95,95);
  sdl_nk_ctx->style.combo.border = 1;
  sdl_nk_ctx->style.combo.rounding = 1;

  /* combo button */
  sdl_nk_ctx->style.combo.button.normal = nk_style_item_color(nk_rgb(216,216,216));
  sdl_nk_ctx->style.combo.button.hover = nk_style_item_color(nk_rgb(216,216,216));
  sdl_nk_ctx->style.combo.button.active = nk_style_item_color(nk_rgb(216,216,216));
  sdl_nk_ctx->style.combo.button.text_background = nk_rgb(216,216,216);
  sdl_nk_ctx->style.combo.button.text_normal = nk_rgb(95,95,95);
  sdl_nk_ctx->style.combo.button.text_hover = nk_rgb(95,95,95);
  sdl_nk_ctx->style.combo.button.text_active = nk_rgb(95,95,95);

  /* property */
  sdl_nk_ctx->style.property.normal = nk_style_item_color(nk_rgb(216,216,216));
  sdl_nk_ctx->style.property.hover = nk_style_item_color(nk_rgb(216,216,216));
  sdl_nk_ctx->style.property.active = nk_style_item_color(nk_rgb(216,216,216));
  sdl_nk_ctx->style.property.border_color = nk_rgb(81,81,81);
  sdl_nk_ctx->style.property.label_normal = nk_rgb(95,95,95);
  sdl_nk_ctx->style.property.label_hover = nk_rgb(95,95,95);
  sdl_nk_ctx->style.property.label_active = nk_rgb(95,95,95);
  sdl_nk_ctx->style.property.sym_left = NK_SYMBOL_TRIANGLE_LEFT;
  sdl_nk_ctx->style.property.sym_right = NK_SYMBOL_TRIANGLE_RIGHT;
  sdl_nk_ctx->style.property.rounding = 10;
  sdl_nk_ctx->style.property.border = 1;

  /* edit */
  sdl_nk_ctx->style.edit.normal = nk_style_item_color(nk_rgb(240,240,240));
  sdl_nk_ctx->style.edit.hover = nk_style_item_color(nk_rgb(240,240,240));
  sdl_nk_ctx->style.edit.active = nk_style_item_color(nk_rgb(240,240,240));
  sdl_nk_ctx->style.edit.border_color = nk_rgb(62,62,62);
  sdl_nk_ctx->style.edit.cursor_normal = nk_rgb(99,202,255);
  sdl_nk_ctx->style.edit.cursor_hover = nk_rgb(99,202,255);
  sdl_nk_ctx->style.edit.cursor_text_normal = nk_rgb(95,95,95);
  sdl_nk_ctx->style.edit.cursor_text_hover = nk_rgb(95,95,95);
  sdl_nk_ctx->style.edit.text_normal = nk_rgb(95,95,95);
  sdl_nk_ctx->style.edit.text_hover = nk_rgb(95,95,95);
  sdl_nk_ctx->style.edit.text_active = nk_rgb(95,95,95);
  sdl_nk_ctx->style.edit.selected_normal = nk_rgb(99,202,255);
  sdl_nk_ctx->style.edit.selected_hover = nk_rgb(99,202,255);
  sdl_nk_ctx->style.edit.selected_text_normal = nk_rgb(95,95,95);
  sdl_nk_ctx->style.edit.selected_text_hover = nk_rgb(95,95,95);
  sdl_nk_ctx->style.edit.border = 1;
  sdl_nk_ctx->style.edit.rounding = 2;

  /* property buttons */
  sdl_nk_ctx->style.property.dec_button.normal = nk_style_item_color(nk_rgb(216,216,216));
  sdl_nk_ctx->style.property.dec_button.hover = nk_style_item_color(nk_rgb(216,216,216));
  sdl_nk_ctx->style.property.dec_button.active = nk_style_item_color(nk_rgb(216,216,216));
  sdl_nk_ctx->style.property.dec_button.text_background = nk_rgba(0,0,0,0);
  sdl_nk_ctx->style.property.dec_button.text_normal = nk_rgb(95,95,95);
  sdl_nk_ctx->style.property.dec_button.text_hover = nk_rgb(95,95,95);
  sdl_nk_ctx->style.property.dec_button.text_active = nk_rgb(95,95,95);
  sdl_nk_ctx->style.property.inc_button = sdl_nk_ctx->style.property.dec_button;

  /* property edit */
  sdl_nk_ctx->style.property.edit.normal = nk_style_item_color(nk_rgb(216,216,216));
  sdl_nk_ctx->style.property.edit.hover = nk_style_item_color(nk_rgb(216,216,216));
  sdl_nk_ctx->style.property.edit.active = nk_style_item_color(nk_rgb(216,216,216));
  sdl_nk_ctx->style.property.edit.border_color = nk_rgba(0,0,0,0);
  sdl_nk_ctx->style.property.edit.cursor_normal = nk_rgb(95,95,95);
  sdl_nk_ctx->style.property.edit.cursor_hover = nk_rgb(95,95,95);
  sdl_nk_ctx->style.property.edit.cursor_text_normal = nk_rgb(216,216,216);
  sdl_nk_ctx->style.property.edit.cursor_text_hover = nk_rgb(216,216,216);
  sdl_nk_ctx->style.property.edit.text_normal = nk_rgb(95,95,95);
  sdl_nk_ctx->style.property.edit.text_hover = nk_rgb(95,95,95);
  sdl_nk_ctx->style.property.edit.text_active = nk_rgb(95,95,95);
  sdl_nk_ctx->style.property.edit.selected_normal = nk_rgb(95,95,95);
  sdl_nk_ctx->style.property.edit.selected_hover = nk_rgb(95,95,95);
  sdl_nk_ctx->style.property.edit.selected_text_normal = nk_rgb(216,216,216);
  sdl_nk_ctx->style.property.edit.selected_text_hover = nk_rgb(216,216,216);

  /* chart */
  sdl_nk_ctx->style.chart.background = nk_style_item_color(nk_rgb(216,216,216));
  sdl_nk_ctx->style.chart.border_color = nk_rgb(81,81,81);
  sdl_nk_ctx->style.chart.color = nk_rgb(95,95,95);
  sdl_nk_ctx->style.chart.selected_color = nk_rgb(255,0,0);
  sdl_nk_ctx->style.chart.border = 1;

  printf("asdfasdf %p\n", &sdl_thread_init);
  pthread_barrier_wait(&sdl_thread_init);
  printf("aeeuoo\n");
  sdl_loop();
}

// main loop for drawing/input thread
void sdl_loop(void) {
  SDL_Event event;
  int r;
  char *s;
  int i, o, c, p = 0;
  bool term = false;

  while (true) {
    if (sdl_nk_func) {
      SDL_EventState(SDL_MOUSEMOTION, SDL_ENABLE);

      nk_input_begin(sdl_nk_ctx);
      while (SDL_PollEvent(&event)) {
        nk_sdl_handle_event(&event);
      }
      nk_input_end(sdl_nk_ctx);
      
      term = sdl_nk_func(sdl_nk_arg);
    } else {
      while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_QUIT:
          break;
        
        case SDL_KEYDOWN:
          switch (event.key.keysym.sym) {
          case SDLK_ESCAPE:
          case SDLK_q:
            //return 'S';
            r = 'S';
            break;
          case SDLK_UP:
            //return 'k';
            r = 'k';
            break;
          case SDLK_LEFT:
            //return 'h';
            r = 'h';
            break;
          case SDLK_DOWN:
            //return 'j';
            r = 'j';
            break;
          case SDLK_RIGHT:
            //return 'l';
            r = 'l';
            break;
          default:
            s = SDL_GetKeyName(event.key.keysym.sym);
            printf("keyname: %s\n", s);
            // check if its string representation has only one character
            // this is to filter out dead keys
            if (!(*(s+1))) {
              r = event.key.keysym.sym;
            }
            break;
          }
          break;

        case SDL_MOUSEBUTTONDOWN:
          sdl_curs_state = true;
          SDL_GetMouseState(&i, &o);
          sdl_curs_x = i;
          sdl_curs_y = o;
          break;
        case SDL_MOUSEBUTTONUP:
          sdl_curs_state = false;
          SDL_GetMouseState(&i, &o);
          sdl_curs_x = i;
          sdl_curs_y = o;
          break;
        case SDL_MOUSEWHEEL:
          if (event.wheel.y > 0) {
            sdl_gl_zoom(NH_SDL_GL_ZOOM);
            sdl_redraw_ = true;
          } else {
            sdl_gl_zoom(-NH_SDL_GL_ZOOM);
            sdl_redraw_ = true;
          }
          break;

        case SDL_CONTROLLERBUTTONDOWN:
          i = sdl_input_controller(&event);
          if (i) r = i;
          break;
        
        case SDL_WINDOWEVENT:
          switch (event.window.event) {
          case SDL_WINDOWEVENT_SHOWN:
          case SDL_WINDOWEVENT_HIDDEN:
          case SDL_WINDOWEVENT_EXPOSED:
          case SDL_WINDOWEVENT_MOVED:
          case SDL_WINDOWEVENT_SIZE_CHANGED:
          case SDL_WINDOWEVENT_RESTORED:
            SDL_GetWindowSize(sdl_window, &i, &o);
            sdl_gl_win_w = i;
            sdl_gl_win_h = o;
            sdl_redraw_ = true;
            break;
          default:
            break;
          }
        }

        if (sdl_curs_state) {
          SDL_GetMouseState(&i, &o);
          if ((i != sdl_curs_x) || (o != sdl_curs_y)) {
            sdl_gl_mat_view[12] += i - sdl_curs_x;
            sdl_gl_mat_view[13] += o - sdl_curs_y;
            sdl_curs_x = i;
            sdl_curs_y = o;
          }
        }
      }

      if (r) {
        write(sdl_thread_fd[1], &r, sizeof(int));
        r = 0;
      }
    }

    if (sdl_state.center) {
      sdl_gl_center(sdl_state.x, sdl_state.y);
    }
    
    sdl_redraw();

    if (term) {
      printf("in term");
      term = false;

      pthread_mutex_lock(&sdl_thread_menu_mutex);
      sdl_nk_arg = NULL;
      sdl_nk_func = NULL;
      pthread_cond_signal(&sdl_thread_menu);
      pthread_mutex_unlock(&sdl_thread_menu_mutex);
    }
  }
}
