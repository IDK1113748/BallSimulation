#define OLC_PGE_APPLICATION
#include "olcPixelGameEngine.h"

#define OLC_SOUNDWAVE
#include "olcSoundWaveEngine.h"

#define WALL_WIDTH 30

//#define BROWNIAN_MOTION

#ifdef BROWNIAN_MOTION
	#define VEL_MIN 1000.0
	#define VEL_MAX 2000.0

	#define RAD_MIN  1.0
	#define RAD_MAX 25.0

	#define N_BALLS 600
#else
	#define VEL_MIN   50.0
	#define VEL_MAX  350.0

	#define RAD_MIN  5.0
	#define RAD_MAX 25.0

	#define N_BALLS 15
#endif

#define ADD_DEL_SHIFT_TIME 0.1f

constexpr double MAX_MASS = (3.14159 * RAD_MAX * RAD_MAX);

double rand_range(double min, double max)
{
	return (max > min ? (min + double(rand() % int(100.0 * (max - min))) / 100.0) : min);
}

class BallSim : public olc::PixelGameEngine
{
public:
	BallSim()
	{
		sAppName = "Ball simulator";
	}

private:

	struct Ball{
		Ball(double x, double y, double vx, double vy, double radius) : mass(3.1415*radius*radius), rad(radius)
		{
			pos.x = x;
			pos.y = y;
			vel.x = vx;
			vel.y = vy;
			color = { (unsigned char)rand_range(100.0,255.0), (unsigned char)rand_range(100.0, 220.0), (unsigned char)rand_range(100.0, 220.0) };
		}
		olc::vf2d pos;
		olc::vf2d vel;
		double mass;
		double rad;
		olc::Pixel color;
	};

	std::vector<Ball> balls;
	std::vector<std::pair<int, int>> possibleCollidIndices;

	bool simPaused = false;
	bool muted = (N_BALLS > 40);

	olc::sound::WaveEngine audioEngine;
	olc::sound::Wave wallHit;
	olc::sound::Wave ballsColllide;

	unsigned int nrPartCollisions = 0;
	unsigned int nrWallCollisions = 0;
	float colElapsedTime = 0.0f;
	float partCollisionsPerSec = 0;
	float wallCollisionsPerSec = 0;

	float addDelElapsedTime = 0.0f;

public:
	void addNewBall(double x, double y)
	{
		double angle = rand() % 628;

#ifdef BROWNIAN_MOTION
		if(rand() % 100 == 0)
			balls.push_back(Ball(x, y, 0.0, 0.0, (int)RAD_MAX));
		else
			balls.push_back(Ball(x, y, rand_range(VEL_MIN, VEL_MAX) * cos(angle), rand_range(VEL_MIN, VEL_MAX) * sin(angle), (int)RAD_MIN));
#else
		balls.push_back(Ball(x, y, rand_range(VEL_MIN, VEL_MAX) * cos(angle), rand_range(VEL_MIN, VEL_MAX) * sin(angle), (int)rand_range(RAD_MIN, RAD_MAX)));
#endif
	}

	void init()
	{
		nrPartCollisions = 0;
		nrWallCollisions = 0;
		colElapsedTime = 0.0f;
		partCollisionsPerSec = 0;
		wallCollisionsPerSec = 0;

		addDelElapsedTime = 0.0f;

		balls.clear();
		for (int i = 0; i < N_BALLS; i++)
		{
			addNewBall(rand_range(WALL_WIDTH, ScreenWidth() - WALL_WIDTH), rand_range(WALL_WIDTH, ScreenHeight() - WALL_WIDTH));
		}
	}

	bool OnUserCreate() override
	{
		audioEngine.InitialiseAudio();
		wallHit.LoadAudioWaveform("metalThump.wav");
		ballsColllide.LoadAudioWaveform("tennisBallTrimmed.wav");

		srand(time(NULL));
		init();
		Clear(olc::BLACK);
		for (auto& b : balls)
		{
			FillCircle(olc::vi2d(b.pos), b.rad, b.color);
		}

		return true;
	}

	void getPossCollisions()
	{
		possibleCollidIndices.clear();
		
		std::sort(balls.begin(), balls.end(), [](Ball a, Ball b) { return a.pos.x < b.pos.x;  });
		for (int active = 0; active < balls.size()-1; active++)
		{
			double activesRightmost = balls[active].pos.x + balls[active].rad;
		
			for (int checked = active + 1; checked < balls.size(); checked++)
			{
				double checkedLeftmost = balls[checked].pos.x - balls[checked].rad;
				if (checkedLeftmost <= activesRightmost)
				{
					possibleCollidIndices.push_back(std::make_pair(active, checked));
				}
			}
		}
	}

	bool OnUserUpdate(float fElapsedTime) override
	{
		if (GetKey(olc::Key::R).bPressed)
			init();
		if (GetKey(olc::Key::M).bPressed)
			muted = !muted;

		bool addDelShiftAllow = false;
		addDelElapsedTime += fElapsedTime;
		if (addDelElapsedTime > ADD_DEL_SHIFT_TIME)
		{
			addDelShiftAllow = true;
			addDelElapsedTime -= ADD_DEL_SHIFT_TIME;
		}

		if (GetMouse(0).bPressed || (addDelShiftAllow && GetMouse(0).bHeld && GetKey(olc::Key::SHIFT).bHeld))
		{
			addNewBall(GetMouseX(), GetMouseY());
		}
		if (GetKey(olc::Key::D).bPressed || (addDelShiftAllow && GetKey(olc::Key::D).bHeld && GetKey(olc::Key::SHIFT).bHeld))
		{
			if(balls.size() > 1)
				balls.erase(balls.begin()+rand()%balls.size());
		}

		colElapsedTime += fElapsedTime;
		if (colElapsedTime >= 3.0f)
		{
			partCollisionsPerSec = (float)nrPartCollisions / colElapsedTime;
			wallCollisionsPerSec = (float)nrWallCollisions / colElapsedTime;
			nrPartCollisions = 0;
			nrWallCollisions = 0;
			colElapsedTime = 0.0f;
		}

		getPossCollisions();
		
		for (const auto& col : possibleCollidIndices)
		{
			double dist = sqrt((balls[col.first].pos.x - balls[col.second].pos.x) * (balls[col.first].pos.x - balls[col.second].pos.x) + (balls[col.first].pos.y - balls[col.second].pos.y) * (balls[col.first].pos.y - balls[col.second].pos.y));
			
			if (dist <= balls[col.first].rad + balls[col.second].rad)
			{
				//collision resolution

				olc::vf2d n = { balls[col.first].pos.x - balls[col.second].pos.x, balls[col.first].pos.y - balls[col.second].pos.y };
				olc::vf2d un = n / (sqrt(n.x*n.x + n.y*n.y));
				olc::vf2d ut = { -un.y, un.x };

				float v1n = balls[col.first].vel.dot(un);
				float v1t = balls[col.first].vel.dot(ut);
				float v2n = balls[col.second].vel.dot(un);
				float v2t = balls[col.second].vel.dot(ut);

				float m1 = balls[col.first].mass;
				float m2 = balls[col.second].mass;
				float v1nPrime = (v1n * (m1 - m2) + 2.0f * m2 * v2n) / (m1 + m2);
				float v2nPrime = (v2n * (m2 - m1) + 2.0f * m1 * v1n) / (m2 + m1);

				balls[col.first].vel  = v1nPrime * un + v1t * ut;
				balls[col.second].vel = v2nPrime * un + v2t * ut;

				float overlap = balls[col.first].rad + balls[col.second].rad - dist;

				balls[col.first].pos  += overlap * un / 2.0f;
				balls[col.second].pos -= overlap * un / 2.0f;

				if(!muted && partCollisionsPerSec < 25)
					audioEngine.PlayWaveform(&ballsColllide, false, (m1*m2) / (MAX_MASS*MAX_MASS));

				nrPartCollisions++;
			}
		}

		if (GetKey(olc::Key::P).bPressed)
			simPaused = !simPaused;

		if (!simPaused)
		{
			for (auto& b : balls)
			{
				bool hit = false;
				if (b.pos.y < WALL_WIDTH + b.rad)
				{
					b.vel.y = -b.vel.y;
					b.pos.y = WALL_WIDTH + b.rad;
					hit = true;
				}
				else if (b.pos.y >= ScreenHeight() - WALL_WIDTH - b.rad)
				{
					b.vel.y = -b.vel.y;
					b.pos.y = ScreenHeight() - WALL_WIDTH - b.rad;
					hit = true;
				}

				if (b.pos.x < WALL_WIDTH + b.rad)
				{
					b.vel.x = -b.vel.x;
					b.pos.x = WALL_WIDTH + b.rad;
					hit = true;
				}
				else if (b.pos.x >= ScreenWidth() - WALL_WIDTH - b.rad)
				{
					b.vel.x = -b.vel.x;
					b.pos.x = ScreenWidth() - WALL_WIDTH - b.rad;
					hit = true;
				}

				if (hit)
				{
					nrWallCollisions++;

					if (!muted && partCollisionsPerSec < 25)
						audioEngine.PlayWaveform(&wallHit, false, b.mass / MAX_MASS);
				}

				b.pos.x += b.vel.x * fElapsedTime;
				b.pos.y += b.vel.y * fElapsedTime;

			}

			Clear(olc::BLACK);

			DrawRect(WALL_WIDTH, WALL_WIDTH, ScreenWidth() - 2 * WALL_WIDTH, ScreenHeight() - 2 * WALL_WIDTH, olc::RED);

			for (auto& b : balls)
			{
				FillCircle(olc::vi2d(b.pos), b.rad, b.color);
			}

			DrawString(1, ScreenHeight() - 27, "Number of balls: " + std::to_string(balls.size()));
			DrawString(1, ScreenHeight() - 18, "Ball collisions per second: " + std::to_string((int)partCollisionsPerSec));
			DrawString(1, ScreenHeight() - 9 , "Wall collisions per second: " + std::to_string((int)wallCollisionsPerSec));

			if (muted)
				DrawString(ScreenWidth() - 41, 1, "Muted");
		}

		return true;
	}
};

int main()
{
	BallSim demo;
	if (demo.Construct(600, 600, 1, 1))
		demo.Start();
	return 0;
}