#include "Mode.hpp"
#include "Scene.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <random>

struct PlayMode : Mode {
	PlayMode();
	virtual ~PlayMode();

	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	struct Button { uint8_t downs=0, pressed=0; } left, right, down, up;

	Scene scene;
	Scene::Camera *camera = nullptr;

	struct Eraser {
		Scene::Transform *transform = nullptr;
		glm::vec2 pos = glm::vec2(0.0f);
		glm::vec2 vel = glm::vec2(0.0f);
		bool alive = true;
	};
	std::vector<Eraser> erasers;

	float physics_accumulator = 0.0f;
	void step_physics(float dt);

	float orbit_angle = -1.5708f;
	float orbit_pitch = 0.6f;
	float orbit_radius = 12.0f;

	bool charging = false;
	float charge = 0.0f;

	enum class Turn { Player, SettleAfterPlayer, EnemyWindup, SettleAfterEnemy, Won, Lost };
	Turn turn = Turn::Player;
	int enemy_idx = -1;
	float windup_timer = 0.0f;
	std::vector<glm::vec2> start_positions;

	bool all_stopped();
	void reset_game();
};