#include "PlayMode.hpp"
#include "LitColorTextureProgram.hpp"
#include "Mesh.hpp"
#include "Load.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"
#include "DrawLines.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <random>

//https://new.gafferongames.com/post/fix_your_timestep/
//https://code.tutsplus.com/how-to-create-a-custom-2d-physics-engine-the-basics-and-impulse-resolution--gamedev-6331t
//https://www.imada.sdu.dk/u/rolf/Edu/DM815/E10/2dcollisions.pdf
//https://www.mbsoftworks.sk/tutorials/opengl4/026-camera-pt3-orbit-camera/

GLuint keshi_vao = 0;

MeshBuffer const *load_keshi_meshes() {
	MeshBuffer const *ret = new MeshBuffer(data_path("keshi.pnct"));
	keshi_vao = ret->make_vao_for_program(lit_color_texture_program->program);
	return ret;
}
Load< MeshBuffer > keshi_meshes(LoadTagDefault, load_keshi_meshes);

void add_drawable(Scene &scene, Scene::Transform *t, Mesh const &mesh) {
	scene.drawables.emplace_back(t);
	Scene::Drawable &d = scene.drawables.back();
	d.pipeline = lit_color_texture_program_pipeline;
	d.pipeline.vao = keshi_vao;
	d.pipeline.type = mesh.type;
	d.pipeline.start = mesh.start;
	d.pipeline.count = mesh.count;
}

void on_scene_mesh(Scene &scene, Scene::Transform *t, std::string const &mesh_name) {
	Mesh const &mesh = keshi_meshes->lookup(mesh_name);
	add_drawable(scene, t, mesh);
}

Scene const *load_keshi_scene() {
	return new Scene(data_path("keshi.scene"), on_scene_mesh);
}
Load< Scene > keshi_scene(LoadTagDefault, load_keshi_scene);

const float PhysicsStep = 1.0f / 120.0f;
const float Friction = 1.5f;
const float EraserRadius = 1.0f;
const float DeskHalf = 15.0f;
const float MaxCharge = 1.5f;
const float MinPower = 3.0f;
const float MaxPower = 25.0f;

std::mt19937 rng(std::random_device{}());

float random_float(float lo, float hi) {
	std::uniform_real_distribution<float> dist(lo, hi);
	return dist(rng);
}

int random_int(int lo, int hi) {
	std::uniform_int_distribution<int> dist(lo, hi);
	return dist(rng);
}

PlayMode::PlayMode() : scene(*keshi_scene) {
	if (scene.cameras.size() != 1) {
		throw std::runtime_error("Expecting one camera.");
	}
	camera = &scene.cameras.front();

	for (auto &t : scene.transforms) {
		if (t.name == "Eraser") {
			t.position = glm::vec3(0.0f, 0.0f, -1000.0f);
		}
	}

	start_positions.push_back(glm::vec2(0.0f, 0.0f));
	start_positions.push_back(glm::vec2(3.5f, 3.5f));
	start_positions.push_back(glm::vec2(-3.5f, 3.5f));
	start_positions.push_back(glm::vec2(-3.5f, -3.5f));
	start_positions.push_back(glm::vec2(3.5f, -3.5f));

	Mesh const &mesh = keshi_meshes->lookup("Cube");
	for (size_t i = 0; i < start_positions.size(); i++) {
		scene.transforms.emplace_back();
		Eraser e;
		e.transform = &scene.transforms.back();
		add_drawable(scene, e.transform, mesh);
		erasers.push_back(e);
	}

	reset_game();
}

PlayMode::~PlayMode() {
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {
	(void)window_size;

	if (evt.type == SDL_EVENT_KEY_DOWN) {
		if (evt.key.key == SDLK_ESCAPE) {
			SDL_SetWindowRelativeMouseMode(Mode::window, false);
			return true;
		}
		if (evt.key.key == SDLK_SPACE) {
			if (!evt.key.repeat) {
				if (turn == Turn::Player) {
					charging = true;
					charge = 0.0f;
					return true;
				}
			}
		}
		if (evt.key.key == SDLK_R) {
			if (turn == Turn::Won || turn == Turn::Lost) {
				reset_game();
				return true;
			}
		}
	}

	if (evt.type == SDL_EVENT_KEY_UP) {
		if (evt.key.key == SDLK_SPACE) {
			if (charging) {
				charging = false;
				if (erasers[0].alive) {
					glm::vec2 dir;
					dir.x = -std::cos(orbit_angle);
					dir.y = -std::sin(orbit_angle);

					float amount = charge / MaxCharge;
					if (amount > 1.0f) {
						amount = 1.0f;
					}

					float power = MinPower + amount * (MaxPower - MinPower);
					erasers[0].vel = dir * power;
					turn = Turn::SettleAfterPlayer;
				}
				return true;
			}
		}
	}

	if (evt.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
		if (!SDL_GetWindowRelativeMouseMode(Mode::window)) {
			SDL_SetWindowRelativeMouseMode(Mode::window, true);
			return true;
		}
	}

	if (evt.type == SDL_EVENT_MOUSE_MOTION) {
		if (SDL_GetWindowRelativeMouseMode(Mode::window)) {
			orbit_angle = orbit_angle - evt.motion.xrel * 0.004f;
			orbit_pitch = orbit_pitch + evt.motion.yrel * 0.004f;
			if (orbit_pitch < 0.15f) {
				orbit_pitch = 0.15f;
			}
			if (orbit_pitch > 1.4f) {
				orbit_pitch = 1.4f;
			}
			return true;
		}
	}

	return false;
}
//https://code.tutsplus.com/how-to-create-a-custom-2d-physics-engine-the-basics-and-impulse-resolution--gamedev-6331t
//https://www.imada.sdu.dk/u/rolf/Edu/DM815/E10/2dcollisions.pdf

// Check every pair of erasers for overlap.
// If they collide, separate them equally and swap their velocity components along the collision normal.
void PlayMode::step_physics(float dt) {
	for (size_t i = 0; i < erasers.size(); i++) {
		if (!erasers[i].alive) {
			continue;
		}

		erasers[i].pos = erasers[i].pos + erasers[i].vel * dt;

		float slow = 1.0f - Friction * dt;
		if (slow < 0.0f) {
			slow = 0.0f;
		}
		erasers[i].vel = erasers[i].vel * slow;

		if (glm::length(erasers[i].vel) <= 0.001f) {
			erasers[i].vel = glm::vec2(0.0f);
		}
	}

	for (size_t i = 0; i < erasers.size(); i++) {
		for (size_t j = i + 1; j < erasers.size(); j++) {
			if (!erasers[i].alive) {
				continue;
			}
			if (!erasers[j].alive) {
				continue;
			}

			glm::vec2 d = erasers[j].pos - erasers[i].pos;
			float dist = glm::length(d);

			if (dist >= 2.0f * EraserRadius) {
				continue;
			}
			if (dist <= 0.0001f) {
				continue;
			}

			glm::vec2 n = d / dist;

			float half_overlap = 0.5f * (2.0f * EraserRadius - dist);
			erasers[i].pos = erasers[i].pos - n * half_overlap;
			erasers[j].pos = erasers[j].pos + n * half_overlap;

			float speed_i = glm::dot(erasers[i].vel, n);
			float speed_j = glm::dot(erasers[j].vel, n);
			float dv = speed_j - speed_i;
			erasers[i].vel = erasers[i].vel + dv * n;
			erasers[j].vel = erasers[j].vel - dv * n;
		}
	}

	for (size_t i = 0; i < erasers.size(); i++) {
		if (std::abs(erasers[i].pos.x) > DeskHalf) {
			erasers[i].alive = false;
		}
		if (std::abs(erasers[i].pos.y) > DeskHalf) {
			erasers[i].alive = false;
		}
	}
}

//https://new.gafferongames.com/post/fix_your_timestep/

// Run physics at a fixed 120 Hz so collision behavior doesn't depend on the rendering frame rate.
void PlayMode::update(float elapsed) {
	physics_accumulator = physics_accumulator + elapsed;
	while (physics_accumulator >= PhysicsStep) {
		step_physics(PhysicsStep);
		physics_accumulator = physics_accumulator - PhysicsStep;
	}

	for (size_t i = 0; i < erasers.size(); i++) {
		if (erasers[i].alive) {
			erasers[i].transform->position = glm::vec3(erasers[i].pos.x, erasers[i].pos.y, 0.7f);
		} else {
			erasers[i].transform->position = glm::vec3(0.0f, 0.0f, -1000.0f);
		}
	}

	if (charging) {
		charge = charge + elapsed;
		if (charge > MaxCharge) {
			charge = MaxCharge;
		}
	}

	//https://www.mbsoftworks.sk/tutorials/opengl4/026-camera-pt3-orbit-camera/

	// Orbit the camera around the player's eraser.
    // orbit_angle controls horizontal rotation and orbit_pitch controls the camera height.
	glm::vec3 target = glm::vec3(erasers[0].pos.x, erasers[0].pos.y, 1.0f);
	glm::vec3 offset;
	offset.x = std::cos(orbit_pitch) * std::cos(orbit_angle);
	offset.y = std::cos(orbit_pitch) * std::sin(orbit_angle);
	offset.z = std::sin(orbit_pitch);
	glm::vec3 cam_pos = target + offset * orbit_radius;

	glm::vec3 up = glm::vec3(0.0f, 0.0f, 1.0f);
	glm::mat3 basis;
	basis[2] = glm::normalize(cam_pos - target);
	basis[0] = glm::normalize(glm::cross(up, basis[2]));
	basis[1] = glm::cross(basis[2], basis[0]);
	camera->transform->position = cam_pos;
	camera->transform->rotation = glm::quat_cast(basis);

	if (turn != Turn::Won && turn != Turn::Lost) {
		if (!erasers[0].alive) {
			turn = Turn::Lost;
		} else {
			bool enemy_left = false;
			for (size_t i = 1; i < erasers.size(); i++) {
				if (erasers[i].alive) {
					enemy_left = true;
				}
			}
			if (!enemy_left) {
				turn = Turn::Won;
			}
		}
	}

	if (turn == Turn::SettleAfterPlayer) {
		if (all_stopped()) {
			std::vector<int> alive_enemies;
			for (size_t i = 1; i < erasers.size(); i++) {
				if (erasers[i].alive) {
					alive_enemies.push_back(int(i));
				}
			}
			if (alive_enemies.size() > 0) {
				int pick = random_int(0, int(alive_enemies.size()) - 1);
				enemy_idx = alive_enemies[pick];
				windup_timer = 1.0f;
				turn = Turn::EnemyWindup;
			}
		}
	} else if (turn == Turn::EnemyWindup) {
		windup_timer = windup_timer - elapsed;

		float hop = 0.4f * std::abs(std::sin(windup_timer * 12.0f));
		erasers[enemy_idx].transform->position.z = erasers[enemy_idx].transform->position.z + hop;

		if (windup_timer <= 0.0f) {
			glm::vec2 d = glm::normalize(erasers[0].pos - erasers[enemy_idx].pos);

			float a = glm::radians(random_float(-15.0f, 15.0f));
			glm::vec2 dir;
			dir.x = std::cos(a) * d.x - std::sin(a) * d.y;
			dir.y = std::sin(a) * d.x + std::cos(a) * d.y;

			float power = random_float(10.0f, 22.0f);
			erasers[enemy_idx].vel = dir * power;
			turn = Turn::SettleAfterEnemy;
		}
	} else if (turn == Turn::SettleAfterEnemy) {
		if (all_stopped()) {
			turn = Turn::Player;
		}
	}
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	float aspect = float(drawable_size.x) / float(drawable_size.y);
	camera->aspect = aspect;

	glUseProgram(lit_color_texture_program->program);
	glUniform1i(lit_color_texture_program->LIGHT_TYPE_int, 1);
	glUniform3fv(lit_color_texture_program->LIGHT_DIRECTION_vec3, 1, glm::value_ptr(glm::vec3(0.0f, 0.0f, -1.0f)));
	glUniform3fv(lit_color_texture_program->LIGHT_ENERGY_vec3, 1, glm::value_ptr(glm::vec3(1.0f, 1.0f, 0.95f)));
	glUseProgram(0);

	glClearColor(0.2f, 0.25f, 0.3f, 1.0f);
	glClearDepth(1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	scene.draw(*camera);
	GL_ERRORS();

	glDisable(GL_DEPTH_TEST);
	DrawLines lines(glm::mat4(
		1.0f / aspect, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f));

	std::string msg = "...";
	if (turn == Turn::Player) {
		if (charging) {
			int percent = int(100.0f * charge / MaxCharge);
			msg = "Power: " + std::to_string(percent) + "%";
		} else {
			msg = "Your turn: hold SPACE to flick, mouse to aim";
		}
	} else if (turn == Turn::EnemyWindup) {
		msg = "Incoming!";
	} else if (turn == Turn::Won) {
		msg = "YOU WIN! Press R to restart";
	} else if (turn == Turn::Lost) {
		msg = "YOU FELL OFF! Press R to restart";
	}

	float H = 0.09f;
	lines.draw_text(msg,
		glm::vec3(-aspect + 0.1f, -0.9f, 0.0f),
		glm::vec3(H, 0.0f, 0.0f),
		glm::vec3(0.0f, H, 0.0f),
		glm::u8vec4(0xff));
}

bool PlayMode::all_stopped() {
	for (size_t i = 0; i < erasers.size(); i++) {
		if (erasers[i].alive) {
			if (glm::length(erasers[i].vel) > 0.1f) {
				return false;
			}
		}
	}
	for (size_t i = 0; i < erasers.size(); i++) {
		erasers[i].vel = glm::vec2(0.0f);
	}
	return true;
}

void PlayMode::reset_game() {
	for (size_t i = 0; i < erasers.size(); i++) {
		erasers[i].pos = start_positions[i];
		erasers[i].vel = glm::vec2(0.0f);
		erasers[i].alive = true;
	}
	turn = Turn::Player;
	charging = false;
	charge = 0.0f;
	enemy_idx = -1;
}