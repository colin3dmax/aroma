
#include "aroma.h"
#include "nacl.lua.h"

namespace aroma {
  void default_flush_callback(void *data, int32_t result) {
    OpenGLContext* context = (OpenGLContext*)data;
    context->render();
  }

	void Renderer::rect(float x1, float y1, float x2, float y2) {
		float colors[] = {
			1,1,1,
			1,1,1,
			1,1,1,
			1,1,1,
		};

		float verts[] = {
			x1,y1,
			x2,y1,
			x1,y2,
			x2,y2
		};

		// GLuint buffs[2];
		GLuint vert_buffer;
		glGenBuffers(1, &vert_buffer);

		glBindBuffer(GL_ARRAY_BUFFER, vert_buffer);
		glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 8, verts, GL_STATIC_DRAW);

		GLuint P = default_shader->attr_loc("P");

		glEnableVertexAttribArray(P);
		glVertexAttribPointer(P, 2, GL_FLOAT, false, 0, 0);

		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glDeleteBuffers(1, &vert_buffer);
	}

	OpenGLContext::OpenGLContext(pp::Instance* instance, Renderer* renderer) :
			pp::Graphics3DClient(instance),
			renderer(renderer),
			instance(instance)
		{
			pp::Module *module = pp::Module::Get();
			assert(module);
			gles2_interface = static_cast<const struct PPB_OpenGLES2*>(
				module->GetBrowserInterface(PPB_OPENGLES2_INTERFACE));
			assert(gles2_interface);
		}

	OpenGLContext::~OpenGLContext() {
		glSetCurrentContextPPAPI(0);
	}

	bool OpenGLContext::make_current() {
		bool is_init = graphics.is_null();
		if (graphics.is_null()) {
			log("init graphics\n");
			int32_t attribs[] = {
				PP_GRAPHICS3DATTRIB_ALPHA_SIZE, 8,
				PP_GRAPHICS3DATTRIB_DEPTH_SIZE, 24,
				PP_GRAPHICS3DATTRIB_STENCIL_SIZE, 8,
				PP_GRAPHICS3DATTRIB_SAMPLES, 0,
				PP_GRAPHICS3DATTRIB_SAMPLE_BUFFERS, 0,
				PP_GRAPHICS3DATTRIB_WIDTH, size.width(),
				PP_GRAPHICS3DATTRIB_HEIGHT, size.height(),
				PP_GRAPHICS3DATTRIB_NONE
			};
			graphics = pp::Graphics3D(instance, pp::Graphics3D(), attribs);
			if (graphics.is_null()) {
				glSetCurrentContextPPAPI(0);
				return false;
			}
			instance->BindGraphics(graphics);
		}

		glSetCurrentContextPPAPI(graphics.pp_resource());

		if (is_init) {
			renderer->init();
		}

		return true;
	}

	void OpenGLContext::resize(const pp::Size& s) {
		log("resize buffers\n");
		size = s;
		if (!graphics.is_null()) {
			graphics.ResizeBuffers(s.width(), s.height());
		}
	}

	void OpenGLContext::flush() {
		graphics.SwapBuffers(pp::CompletionCallback(&default_flush_callback, this));
	}

	void OpenGLContext::Graphics3DContextLost() {
		assert(!"++ Lost graphics context");
	}

	void OpenGLContext::render() {
		renderer->tick();
	}

	int OpenGLContext::width() {
		return size.width();
	}

	int OpenGLContext::height() {
		return size.height();
	}

	//
	// Renderer
	//

	Renderer::Renderer(pp::Instance* instance) :
		instance(instance),
		default_shader(NULL)
	{
		context = new OpenGLContext(instance, this);
	}
	
	void Renderer::init() {
		log("init renderer\n");
		glClearColor(0.1, 0.1, 0.1, 1.0);

		const char *vertex_src =
			"attribute vec2 P;\n"
			"void main(void) {\n"
			"  gl_Position = vec4(P, 0.0, 1.0);\n"
			"}\n"
			;

		const char *fragment_src =
			"void main(void) {\n"
			"	 gl_FragColor = vec4(1,0,1,1);\n"
			"}\n"
			;

		default_shader = new Shader();
		default_shader->add(GL_VERTEX_SHADER, vertex_src);
		default_shader->add(GL_FRAGMENT_SHADER, fragment_src);
		default_shader->link();
	}

	void Renderer::draw() {
		glClear(GL_COLOR_BUFFER_BIT);
		default_shader->bind();
		rect(0,0, 1,1);
	}

	// called for every frame
	void Renderer::tick() {
		context->make_current();

		glViewport(0, 0, context->width(), context->height());

		draw();
		context->flush();
	}

	void Renderer::did_change_view(const pp::Rect& pos, const pp::Rect& clip) {
		context->resize(pos.size());
		tick();
	}


  void push_var(lua_State* l, pp::Var var) {
    if (var.is_null()) {
      lua_pushnil(l);
    } else if (var.is_number()) {
      lua_pushnumber(l, var.AsDouble());
    } else if (var.is_bool()) {
      lua_pushboolean(l, var.AsBool());
    } else {
      lua_pushstring(l, var.AsString().c_str());
    }
  }

  pp::Var to_var(lua_State* l, int index) {
    if (lua_isnil(l, index)) {
      return pp::Var(pp::Var::Null());
    } else if (lua_isnumber(l, index)) {
      return pp::Var(lua_tonumber(l, index));
    } else if (lua_isboolean(l, index)) {
      return pp::Var((bool)lua_toboolean(l, index));
    }

    return pp::Var(lua_tostring(l, index));
  }

  int _post_message(lua_State *l) {
    pp::Instance *instance  = (pp::Instance*)lua_touserdata(l, lua_upvalueindex(1));
    instance->PostMessage(to_var(l, 1));
    return 0;
  }

  int _sleep(lua_State *l) {
    sleep(luaL_checknumber(l, 1));
    return 0;
  }

  void sleep(float seconds) {
    long nanoseconds = (long)(seconds * 1000000000);
    timespec req = { 0, nanoseconds };
    nanosleep(&req, NULL);
  }

  class AromaInstance : public pp::Instance {
    protected:
      lua_State* l;
      Renderer *renderer;

      void bind_function(const char *name, lua_CFunction func) {
        lua_getglobal(l, "nacl");
        lua_pushlightuserdata(l, this);
        lua_pushcclosure(l, func, 1);
        lua_setfield(l, -2, name);
        lua_pop(l, 1);
      }

      // takes value on top of stack and puts it in package.loaded[name]
      // pops value
      void preload_library(const char *name) {
        int i = lua_gettop(l);
        lua_getglobal(l, "package");
        lua_getfield(l, -1, "loaded");
        lua_pushvalue(l, i);
        lua_setfield(l, -2, name);
        lua_settop(l, i - 1);
      }

    public:
      AromaInstance(PP_Instance instance) :
        pp::Instance(instance),
        renderer(NULL) { }

      bool Init(uint32_t argc, const char** argn, const char** argv) {
        l = luaL_newstate();
        luaL_openlibs(l);

        luaopen_cjson(l);
        preload_library("cjson");

        lua_newtable(l);
        lua_setglobal(l, "nacl");

        bind_function("post_message", _post_message);
        bind_function("sleep", _sleep);

        lua_settop(l, 0);

        if (luaL_loadbuffer(l, (const char*)lib_nacl_lua, lib_nacl_lua_len, "nacl.lua") != 0) {
          log("%s\n", luaL_checkstring(l, -1));
          return false;
        }

        if (lua_pcall(l, 0, 0, 0) != 0) {
          log("%s\n", luaL_checkstring(l, -1));
          return false;
        }

        PostMessage(pp::Var("Lua loaded"));
        return true;
      }

      virtual ~AromaInstance() { }

      void HandleMessage(const pp::Var& var) {
        lua_getglobal(l, "nacl");
        lua_getfield(l, -1, "handle_message");
        if (lua_isnil(l, -1)) {
					log("Ignoring message, missing `nacl.handle_message`\n");
          return;
        }
        push_var(l, var);
        lua_call(l, 1, 0);
      }

      void DidChangeView(const pp::Rect& pos, const pp::Rect& clip) {
        // PostMessage(pp::Var("DidChangeView"));
        log("didchangeview\n");
        if (!renderer) renderer = new Renderer(this);
        renderer->did_change_view(pos, clip);
      }

      lua_State* lua() {
        return l;
      }
  };

  class AromaModule : public pp::Module {
    public:
      AromaModule() : pp::Module() {}
      virtual ~AromaModule() {}

      virtual bool Init() {
        return glInitializePPAPI(get_browser_interface()) == GL_TRUE;
      }

      virtual pp::Instance* CreateInstance(PP_Instance instance) {
        return new AromaInstance(instance);
      }
  };
}

namespace pp {
  Module* CreateModule() {
    return new aroma::AromaModule();
  }
}