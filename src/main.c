#include <SDL2/SDL.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define WINDOW_WIDTH 1024
#define WINDOW_HEIGHT 600

#define COURT_MIN_X 0.0f
#define COURT_MAX_X ((float)WINDOW_WIDTH)
#define NET_X 512.0f
#define NET_TOP_Y 320.0f
#define FLOOR_Y 560.0f

#define PLAYER_W 26.0f
#define PLAYER_H 160.0f
#define HEAD_RADIUS 20.0f
#define HAND_HIT_RADIUS 10.0f
#define ARM_THICKNESS 12
#define HAND_OVAL_RX 12
#define HAND_OVAL_RY 8
#define PLAYER_SPEED 300.0f
#define PLAYER_BLOCK_TIME 0.16f
#define PLAYER_BLOCK_TIME_MIN 0.08f
#define PLAYER_JUMP_SPEED -525.0f
#define PLAYER_GRAVITY 1080.0f
#define PLAYER_JUMP_HOLD_TIME 0.16f
#define PLAYER_JUMP_HOLD_GRAVITY_MULT 0.46f
#define PLAYER_JUMP_RELEASE_GRAVITY_MULT 1.72f
#define CPU_BLOCK_TIME 0.14f
#define CPU_JUMP_SPEED -430.0f
#define CPU_SPEED 260.0f
#define CPU_HIT_COOLDOWN 0.12f

#define BALL_RADIUS 20.0f
#define GRAVITY 860.0f
#define RETURN_SPEED_X 270.0f
#define RETURN_SPEED_Y -520.0f
#define POINTS_TO_WIN_SET 25
#define MIN_LEAD_TO_WIN_SET 2
#define MAX_TOUCHES_PER_SIDE 3
#define TOUCH_MIN_INTERVAL 0.5f
#define TOUCH_MIN_RISE_PX 30.0f
#define SERVE_CHARGE_MAX_TIME 1.2f

#define FIXED_DT (1.0f / 120.0f)
#define MAX_ACCUMULATED_TIME 0.25f

#define AUDIO_SAMPLE_RATE 22050
#define AUDIO_AMPLITUDE 1800
#define MAX_HIGHSCORES 7
#define HIGHSCORE_NAME_LEN 10

typedef struct Vec2 {
    float x;
    float y;
} Vec2;

typedef struct Ball {
    Vec2 pos;
    Vec2 vel;
} Ball;

typedef struct Player {
    float x;
    float y;
    float vy;
    float jumpHoldTimer;
    bool onGround;
    bool isBlocking;
    float blockTimer;
} Player;

typedef struct Audio {
    SDL_AudioDeviceID device;
    SDL_AudioSpec spec;
    bool muted;
} Audio;

typedef struct HighscoreEntry {
    char name[HIGHSCORE_NAME_LEN + 1];
    int setsFor;
    int setsAgainst;
    int pointsFor;
    int pointsAgainst;
} HighscoreEntry;

typedef enum Scene {
    SCENE_START = 0,
    SCENE_PLAYING = 1,
    SCENE_PAUSED = 2,
    SCENE_GAME_OVER = 3,
    SCENE_NAME_ENTRY = 4
} Scene;

enum GameEvent {
    EVENT_NONE = 0,
    EVENT_WALL_BOUNCE = 1 << 0,
    EVENT_BLOCK_SUCCESS = 1 << 1,
    EVENT_PLAYER_RECEIVE = 1 << 2,
    EVENT_CPU_RETURN = 1 << 3,
    EVENT_MISS = 1 << 4,
    EVENT_POINT = 1 << 5,
    EVENT_GAME_OVER = 1 << 6
};

typedef struct Game {
    Ball ball;
    Player player;
    Player cpu;
    float cpuHitCooldown;
    float elapsedSeconds;
    float blockWindow;
    float cpuSpeedScale;
    int level;
    int playerPoints;
    int cpuPoints;
    int playerSets;
    int cpuSets;
    int highscoreSets;
    int touchesLeft;
    int touchesRight;
    int cpuServeVariant;
    int lastTouchSide;
    int ballSide;
    int serverSide;
    int difficulty;
    bool twoPlayerMode;
    bool waitingServe;
    float cpuServeTimer;
    bool serveCharging;
    float serveCharge;
    bool serveOutOnMax;
    bool touchGateActive;
    bool touchRiseReached;
    float touchTimer;
    float touchStartY;
} Game;

static void draw_filled_circle(SDL_Renderer *renderer, int cx, int cy, int radius);

static float clampf(float value, float min, float max) {
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

static int load_highscores(HighscoreEntry *entries, int maxEntries) {
    FILE *f = fopen("highscores.dat", "r");
    int count = 0;

    if (!f) {
        return 0;
    }

    while (count < maxEntries) {
        HighscoreEntry e;
        int parsed = fscanf(f, "%10s %d %d %d %d", e.name, &e.setsFor, &e.setsAgainst, &e.pointsFor, &e.pointsAgainst);
        if (parsed < 2) {
            break;
        }
        if (parsed == 2) {
            e.setsAgainst = 0;
            e.pointsFor = 0;
            e.pointsAgainst = 0;
        } else if (parsed == 3) {
            e.setsAgainst = 0;
            e.pointsAgainst = 0;
        } else if (parsed == 4) {
            e.pointsAgainst = 0;
        }
        if (e.setsFor < 0) {
            e.setsFor = 0;
        }
        if (e.setsAgainst < 0) {
            e.setsAgainst = 0;
        }
        if (e.pointsFor < 0) {
            e.pointsFor = 0;
        }
        if (e.pointsAgainst < 0) {
            e.pointsAgainst = 0;
        }
        entries[count++] = e;
    }

    fclose(f);
    return count;
}

static void save_highscores(const HighscoreEntry *entries, int count) {
    FILE *f = fopen("highscores.dat", "w");
    if (!f) {
        return;
    }

    for (int i = 0; i < count; ++i) {
        fprintf(
            f,
            "%s %d %d %d %d\n",
            entries[i].name,
            entries[i].setsFor,
            entries[i].setsAgainst,
            entries[i].pointsFor,
            entries[i].pointsAgainst
        );
    }
    fclose(f);
}

static bool highscore_better_than(
    int setsFor,
    int setsAgainst,
    int pointsFor,
    int pointsAgainst,
    const HighscoreEntry *entry
) {
    int setDiff = setsFor - setsAgainst;
    int entrySetDiff = entry->setsFor - entry->setsAgainst;
    int pointDiff = pointsFor - pointsAgainst;
    int entryPointDiff = entry->pointsFor - entry->pointsAgainst;

    if (setsFor != entry->setsFor) {
        return setsFor > entry->setsFor;
    }
    if (setDiff != entrySetDiff) {
        return setDiff > entrySetDiff;
    }
    if (pointsFor != entry->pointsFor) {
        return pointsFor > entry->pointsFor;
    }
    return pointDiff > entryPointDiff;
}

static void insert_highscore(
    HighscoreEntry *entries,
    int *count,
    const char *name,
    int setsFor,
    int setsAgainst,
    int pointsFor,
    int pointsAgainst
) {
    int n = *count;
    if (setsFor < 0) {
        setsFor = 0;
    }
    if (setsAgainst < 0) {
        setsAgainst = 0;
    }
    if (pointsFor < 0) {
        pointsFor = 0;
    }
    if (pointsAgainst < 0) {
        pointsAgainst = 0;
    }

    if (n < MAX_HIGHSCORES) {
        entries[n].setsFor = setsFor;
        entries[n].setsAgainst = setsAgainst;
        entries[n].pointsFor = pointsFor;
        entries[n].pointsAgainst = pointsAgainst;
        snprintf(entries[n].name, sizeof(entries[n].name), "%s", name);
        n += 1;
    } else if (highscore_better_than(setsFor, setsAgainst, pointsFor, pointsAgainst, &entries[n - 1])) {
        entries[n - 1].setsFor = setsFor;
        entries[n - 1].setsAgainst = setsAgainst;
        entries[n - 1].pointsFor = pointsFor;
        entries[n - 1].pointsAgainst = pointsAgainst;
        snprintf(entries[n - 1].name, sizeof(entries[n - 1].name), "%s", name);
    } else {
        *count = n;
        return;
    }

    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            if (highscore_better_than(
                    entries[j].setsFor,
                    entries[j].setsAgainst,
                    entries[j].pointsFor,
                    entries[j].pointsAgainst,
                    &entries[i])) {
                HighscoreEntry tmp = entries[i];
                entries[i] = entries[j];
                entries[j] = tmp;
            }
        }
    }

    if (n > MAX_HIGHSCORES) {
        n = MAX_HIGHSCORES;
    }
    *count = n;
}

static char scancode_to_name_char(SDL_Scancode sc) {
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) {
        return (char)('A' + (sc - SDL_SCANCODE_A));
    }
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) {
        return (char)('1' + (sc - SDL_SCANCODE_1));
    }
    if (sc == SDL_SCANCODE_0) {
        return '0';
    }
    return '\0';
}

static void refresh_difficulty(Game *game) {
    int level = 1 + (int)(game->elapsedSeconds / 60.0f);
    float baseScale = 1.0f;
    if (level > 8) {
        level = 8;
    }

    if (game->difficulty <= 0) {
        baseScale = 0.70f;
    } else if (game->difficulty >= 2) {
        baseScale = 1.40f;
    }

    game->level = level;
    game->cpuSpeedScale = baseScale * (1.0f + 0.08f * (float)(level - 1));

    {
        float tightened = PLAYER_BLOCK_TIME - 0.01f * (float)(level - 1);
        game->blockWindow = clampf(tightened, PLAYER_BLOCK_TIME_MIN, PLAYER_BLOCK_TIME);
    }
}

static void queue_tone(Audio *audio, float frequency, int durationMs, float volumeScale) {
    if (audio->device == 0) {
        return;
    }

    int samples = (AUDIO_SAMPLE_RATE * durationMs) / 1000;
    if (samples <= 0) {
        return;
    }

    int16_t *buffer = (int16_t *)malloc((size_t)samples * sizeof(int16_t));
    if (!buffer) {
        return;
    }

    float phase = 0.0f;
    float step = frequency / (float)AUDIO_SAMPLE_RATE;
    float amp = (float)AUDIO_AMPLITUDE * volumeScale;

    for (int i = 0; i < samples; ++i) {
        float env = 1.0f - ((float)i / (float)samples);
        float sample = (phase < 0.5f ? 1.0f : -1.0f) * amp * env;
        buffer[i] = (int16_t)sample;
        phase += step;
        if (phase >= 1.0f) {
            phase -= 1.0f;
        }
    }

    SDL_QueueAudio(audio->device, buffer, (Uint32)((size_t)samples * sizeof(int16_t)));
    free(buffer);
}

static void play_events(Audio *audio, unsigned events) {
    if (audio->muted) {
        return;
    }

    if (events & EVENT_BLOCK_SUCCESS) {
        queue_tone(audio, 880.0f, 55, 1.0f);
        queue_tone(audio, 1040.0f, 45, 0.9f);
    }
    if (events & EVENT_CPU_RETURN) {
        queue_tone(audio, 660.0f, 45, 0.75f);
    }
    if (events & EVENT_PLAYER_RECEIVE) {
        queue_tone(audio, 420.0f, 40, 0.65f);
    }
    if (events & EVENT_WALL_BOUNCE) {
        queue_tone(audio, 320.0f, 20, 0.5f);
    }
    if (events & EVENT_MISS) {
        queue_tone(audio, 170.0f, 100, 1.0f);
    }
    if (events & EVENT_POINT) {
        queue_tone(audio, 760.0f, 70, 0.85f);
    }
}

static float point_segment_distance_sq(float px, float py, float ax, float ay, float bx, float by) {
    float abx = bx - ax;
    float aby = by - ay;
    float apx = px - ax;
    float apy = py - ay;
    float abLen2 = abx * abx + aby * aby;
    float t;

    if (abLen2 <= 0.0001f) {
        float dx = px - ax;
        float dy = py - ay;
        return dx * dx + dy * dy;
    }

    t = (apx * abx + apy * aby) / abLen2;
    t = clampf(t, 0.0f, 1.0f);

    {
        float cx = ax + t * abx;
        float cy = ay + t * aby;
        float dx = px - cx;
        float dy = py - cy;
        return dx * dx + dy * dy;
    }
}

static void compute_arm_pose(float bodyX, float bodyY, bool towardRight, bool armsUp, float *shoulderX, float *shoulderY, float *handX, float *handY) {
    float dir = towardRight ? 1.0f : -1.0f;
    float sx = bodyX + PLAYER_W * 0.5f + dir * 3.0f;
    float sy = bodyY + 50.0f;
    float hx;
    float hy;

    if (armsUp) {
        hx = sx + dir * 4.0f;
        hy = sy - 100.0f;
    } else {
        hx = sx + dir * 34.0f;
        hy = sy + 36.0f;
    }

    *shoulderX = sx;
    *shoulderY = sy;
    *handX = hx;
    *handY = hy;
}

static void draw_rotated_filled_ellipse(SDL_Renderer *renderer, int cx, int cy, int rx, int ry, float angleRad) {
    if (rx <= 0 || ry <= 0) {
        return;
    }

    {
        int r = rx > ry ? rx : ry;
        float c = cosf(angleRad);
        float s = sinf(angleRad);
        float invRx2 = 1.0f / ((float)rx * (float)rx);
        float invRy2 = 1.0f / ((float)ry * (float)ry);

        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                float ux = c * (float)dx + s * (float)dy;
                float uy = -s * (float)dx + c * (float)dy;
                float eq = ux * ux * invRx2 + uy * uy * invRy2;
                if (eq <= 1.0f) {
                    SDL_RenderDrawPoint(renderer, cx + dx, cy + dy);
                }
            }
        }
    }
}

static void draw_thick_segment(SDL_Renderer *renderer, float ax, float ay, float bx, float by, int thickness) {
    float dx = bx - ax;
    float dy = by - ay;
    float len = sqrtf(dx * dx + dy * dy);
    int radius = thickness / 2;
    int steps;

    if (len < 0.001f) {
        draw_filled_circle(renderer, (int)ax, (int)ay, radius);
        return;
    }

    steps = (int)len;
    if (steps < 1) {
        steps = 1;
    }

    for (int i = 0; i <= steps; ++i) {
        float t = (float)i / (float)steps;
        float px = ax + dx * t;
        float py = ay + dy * t;
        draw_filled_circle(renderer, (int)px, (int)py, radius);
    }
}

static void draw_arm_with_hand(
    SDL_Renderer *renderer,
    float shoulderX,
    float shoulderY,
    float handX,
    float handY,
    bool towardRight,
    SDL_Color armColor,
    SDL_Color handColor
) {
    float armAngle = atan2f(handY - shoulderY, handX - shoulderX);
    float handAngle = armAngle + (towardRight ? 0.4363323f : -0.4363323f);
    float handCx = handX + cosf(armAngle) * 4.0f;
    float handCy = handY + sinf(armAngle) * 4.0f;

    SDL_SetRenderDrawColor(renderer, armColor.r, armColor.g, armColor.b, armColor.a);
    draw_thick_segment(renderer, shoulderX, shoulderY, handX, handY, ARM_THICKNESS);

    SDL_SetRenderDrawColor(renderer, handColor.r, handColor.g, handColor.b, handColor.a);
    draw_rotated_filled_ellipse(renderer, (int)handCx, (int)handCy, HAND_OVAL_RX, HAND_OVAL_RY, handAngle);
}

static bool reflect_ball_on_head_zone(Ball *ball, float prevX, float prevY, float headX, float headY, bool towardRight, float hitterVy, bool hitterOnGround) {
    float sumR = BALL_RADIUS + HEAD_RADIUS;
    float sumR2 = sumR * sumR;
    float dist2 = point_segment_distance_sq(headX, headY, prevX, prevY, ball->pos.x, ball->pos.y);

    if (dist2 > sumR2) {
        return false;
    }

    {
        float nx = ball->pos.x - headX;
        float ny = ball->pos.y - headY;
        float nLen2 = nx * nx + ny * ny;
        float nLen;

        if (nLen2 < 0.0001f) {
            nx = towardRight ? 1.0f : -1.0f;
            ny = -0.1f;
            nLen = sqrtf(nx * nx + ny * ny);
        } else {
            nLen = sqrtf(nLen2);
        }

        nx /= nLen;
        ny /= nLen;

        ball->pos.x = headX + nx * (sumR + 1.0f);
        ball->pos.y = headY + ny * (sumR + 1.0f);
    }

    {
        float hitT = (ball->pos.x - (headX - HEAD_RADIUS)) / (2.0f * HEAD_RADIUS);
        float frontness;
        float vxMag;
        float vyMag;
        float jumpBoost;
        float upwardFactor;
        bool backHeadHit;

        hitT = clampf(hitT, 0.0f, 1.0f);

        /* Front edge toward opponent: flatter/faster; back edge: shorter/higher. */
        frontness = towardRight ? hitT : (1.0f - hitT);
        vxMag = 150.0f + frontness * 280.0f;
        vyMag = 530.0f - frontness * 310.0f;

        /* Airborne contacts are stronger; rising jump gives an extra punch. */
        jumpBoost = 1.0f;
        if (!hitterOnGround) {
            upwardFactor = clampf((-hitterVy) / 520.0f, 0.0f, 1.0f);
            jumpBoost = 1.12f + 0.18f * upwardFactor;
            vxMag *= jumpBoost;
            vyMag *= jumpBoost;
        }

        /* Rear quarter of the head sends the ball backwards. */
        backHeadHit = frontness < 0.25f;

        if (backHeadHit) {
            ball->vel.x = towardRight ? -vxMag : vxMag;
        } else {
            ball->vel.x = towardRight ? vxMag : -vxMag;
        }
        ball->vel.y = -vyMag;
    }

    return true;
}

static bool reflect_ball_on_hand_zone(Ball *ball, float prevX, float prevY, float handX, float handY, bool towardRight, float hitterVy, bool hitterOnGround) {
    float sumR = BALL_RADIUS + HAND_HIT_RADIUS;
    float sumR2 = sumR * sumR;
    float dist2 = point_segment_distance_sq(handX, handY, prevX, prevY, ball->pos.x, ball->pos.y);

    if (dist2 > sumR2) {
        return false;
    }

    {
        float nx = ball->pos.x - handX;
        float ny = ball->pos.y - handY;
        float nLen2 = nx * nx + ny * ny;
        float nLen;

        if (nLen2 < 0.0001f) {
            nx = towardRight ? 1.0f : -1.0f;
            ny = -0.18f;
            nLen = sqrtf(nx * nx + ny * ny);
        } else {
            nLen = sqrtf(nLen2);
        }

        nx /= nLen;
        ny /= nLen;
        ball->pos.x = handX + nx * (sumR + 1.0f);
        ball->pos.y = handY + ny * (sumR + 1.0f);
    }

    {
        float handHit = (ball->pos.y - (handY - HAND_HIT_RADIUS)) / (2.0f * HAND_HIT_RADIUS);
        float topHit;
        float vxMag;
        float vyMag;
        float jumpBoost = 1.0f;

        handHit = clampf(handHit, 0.0f, 1.0f);
        topHit = 1.0f - handHit;

        vxMag = 240.0f + 210.0f * topHit;
        vyMag = 360.0f + 240.0f * topHit;

        if (!hitterOnGround) {
            float upwardFactor = clampf((-hitterVy) / 560.0f, 0.0f, 1.0f);
            jumpBoost = 1.06f + 0.20f * upwardFactor;
            vxMag *= jumpBoost;
            vyMag *= jumpBoost;
        }

        ball->vel.x = towardRight ? vxMag : -vxMag;
        ball->vel.y = -vyMag;
    }

    return true;
}

static void place_ball_in_server_hand(Game *game) {
    if (game->serverSide < 0) {
        float hx = game->player.x + PLAYER_W * 0.5f;
        float hy = game->player.y;
        game->ball.pos.x = hx + HEAD_RADIUS + BALL_RADIUS - 3.0f;
        game->ball.pos.y = hy - (HEAD_RADIUS + BALL_RADIUS - 3.0f);
    } else {
        float hx = game->cpu.x + PLAYER_W * 0.5f;
        float hy = game->cpu.y;
        game->ball.pos.x = hx - (HEAD_RADIUS + BALL_RADIUS - 3.0f);
        game->ball.pos.y = hy - (HEAD_RADIUS + BALL_RADIUS - 3.0f);
    }
    game->ball.vel.x = 0.0f;
    game->ball.vel.y = 0.0f;
}

static void start_serve(Game *game, int side) {
    int v;
    float serveScale = 1.0f;

    if (!game->waitingServe || game->serverSide != side) {
        return;
    }

    game->waitingServe = false;
    game->touchesLeft = 0;
    game->touchesRight = 0;
    game->ballSide = side;
    game->lastTouchSide = side;

    if (side < 0) {
        game->ball.vel.x = 360.0f;
        game->ball.vel.y = -430.0f;
    } else {
        serveScale = clampf(game->cpuSpeedScale, 0.8f, 1.9f);
        /* Cycle through a few serve trajectories so CPU serves are varied and net-safe. */
        v = game->cpuServeVariant % 4;
        if (v == 0) {
            game->ball.vel.x = -560.0f * serveScale;
            game->ball.vel.y = -430.0f * serveScale;
        } else if (v == 1) {
            game->ball.vel.x = -520.0f * serveScale;
            game->ball.vel.y = -500.0f * serveScale;
        } else if (v == 2) {
            game->ball.vel.x = -620.0f * serveScale;
            game->ball.vel.y = -390.0f * serveScale;
        } else {
            game->ball.vel.x = -540.0f * serveScale;
            game->ball.vel.y = -460.0f * serveScale;
        }
        game->cpuServeVariant = (game->cpuServeVariant + 1) % 4;
    }

    game->touchGateActive = false;
    game->touchRiseReached = true;
    game->touchTimer = TOUCH_MIN_INTERVAL;
    game->serveCharging = false;
    game->serveCharge = 0.0f;
    game->serveOutOnMax = false;
}

static void start_human_charged_serve(Game *game, int side) {
    float c;
    float jumpHeight;
    float jumpInfluence;
    float hitterY;
    float hitterVy;
    float vx;

    if (!game->waitingServe || game->serverSide != side) {
        return;
    }

    c = clampf(game->serveCharge, 0.0f, 1.0f);

    game->waitingServe = false;
    game->touchesLeft = 0;
    game->touchesRight = 0;
    game->ballSide = side;
    game->lastTouchSide = side;

    if (side < 0) {
        hitterY = game->player.y;
        hitterVy = game->player.vy;
    } else {
        hitterY = game->cpu.y;
        hitterVy = game->cpu.vy;
    }

    game->serveOutOnMax = (c >= 0.999f);
    if (game->serveOutOnMax) {
        game->ball.vel.x = (side < 0) ? 760.0f : -760.0f;
        game->ball.vel.y = -120.0f;
    } else {
        jumpHeight = (FLOOR_Y - PLAYER_H) - hitterY;
        jumpInfluence = clampf(jumpHeight / 90.0f, 0.0f, 1.0f);

        /* CTRL sets distance; optional jump timing nudges range and arc shape. */
        vx = 300.0f + 340.0f * c + 90.0f * jumpInfluence;
        game->ball.vel.x = (side < 0) ? vx : -vx;
        game->ball.vel.y = -(560.0f - 170.0f * c) + 0.25f * hitterVy - 45.0f * jumpInfluence;
    }

    game->touchGateActive = false;
    game->touchRiseReached = true;
    game->touchTimer = TOUCH_MIN_INTERVAL;
    game->serveCharging = false;
    game->serveCharge = 0.0f;
}

static void reset_rally(Game *game) {
    game->player.x = COURT_MIN_X + 40.0f;
    game->player.y = FLOOR_Y - PLAYER_H;
    game->player.vy = 0.0f;
    game->player.jumpHoldTimer = 0.0f;
    game->player.onGround = true;
    game->player.isBlocking = false;
    game->player.blockTimer = 0.0f;

    game->cpu.x = COURT_MAX_X - PLAYER_W - 24.0f;
    game->cpu.y = FLOOR_Y - PLAYER_H;
    game->cpu.vy = 0.0f;
    game->cpu.jumpHoldTimer = 0.0f;
    game->cpu.onGround = true;
    game->cpu.isBlocking = false;
    game->cpu.blockTimer = 0.0f;
    game->cpuHitCooldown = 0.0f;

    game->touchesLeft = 0;
    game->touchesRight = 0;
    game->ballSide = game->serverSide;
    game->lastTouchSide = 0;
    game->waitingServe = true;
    if (game->difficulty <= 0) {
        game->cpuServeTimer = 0.82f;
    } else if (game->difficulty >= 2) {
        game->cpuServeTimer = 0.34f;
    } else {
        game->cpuServeTimer = 0.55f;
    }
    game->serveCharging = false;
    game->serveCharge = 0.0f;
    game->serveOutOnMax = false;
    game->touchGateActive = false;
    game->touchRiseReached = true;
    game->touchTimer = TOUCH_MIN_INTERVAL;

    place_ball_in_server_hand(game);
    game->touchStartY = game->ball.pos.y;
}

static bool can_touch_ball(const Game *game) {
    if (!game->touchGateActive) {
        return true;
    }
    return game->touchTimer >= TOUCH_MIN_INTERVAL && game->touchRiseReached;
}

static void register_ball_touch(Game *game) {
    game->touchGateActive = true;
    game->touchRiseReached = false;
    game->touchTimer = 0.0f;
    game->touchStartY = game->ball.pos.y;
}

static void award_point(Game *game, bool playerWon, unsigned *events) {
    if (playerWon) {
        game->playerPoints += 1;
        game->serverSide = -1;
        *events |= EVENT_POINT;
    } else {
        game->cpuPoints += 1;
        game->serverSide = 1;
        *events |= EVENT_MISS;
    }

    if ((game->playerPoints >= POINTS_TO_WIN_SET || game->cpuPoints >= POINTS_TO_WIN_SET) &&
        abs(game->playerPoints - game->cpuPoints) >= MIN_LEAD_TO_WIN_SET) {
        if (game->playerPoints > game->cpuPoints) {
            game->playerSets += 1;
            if (game->playerSets > game->highscoreSets) {
                game->highscoreSets = game->playerSets;
            }
        } else {
            game->cpuSets += 1;
        }

        game->playerPoints = 0;
        game->cpuPoints = 0;
    }

    reset_rally(game);
}

static void init_game(Game *game, bool twoPlayerMode, int difficulty) {
    game->twoPlayerMode = twoPlayerMode;
    if (difficulty < 0) {
        game->difficulty = 0;
    } else if (difficulty > 2) {
        game->difficulty = 2;
    } else {
        game->difficulty = difficulty;
    }
    game->playerPoints = 0;
    game->cpuPoints = 0;
    game->playerSets = 0;
    game->cpuSets = 0;
    game->cpuServeVariant = 0;
    game->elapsedSeconds = 0.0f;
    game->level = 1;
    game->blockWindow = PLAYER_BLOCK_TIME;
    game->cpuSpeedScale = 1.0f;
    game->serverSide = 1;
    refresh_difficulty(game);
    reset_rally(game);
}

static unsigned update_game(Game *game, float dt, const Uint8 *keys) {
    unsigned events = EVENT_NONE;
    float aiFactor = 1.0f;
    bool shouldCpuBlock = false;
    bool shouldCpuJump = false;
    float cpuHeadX;
    float cpuHeadY;
    float playerHeadX;
    float playerHeadY;
    float playerShoulderX;
    float playerShoulderY;
    float playerHandX;
    float playerHandY;
    float cpuShoulderX;
    float cpuShoulderY;
    float cpuHandX;
    float cpuHandY;
    bool playerJumpHeld;
    bool rightJumpHeld;
    float playerGravityScale;
    float rightGravityScale;

    game->elapsedSeconds += dt;

    if (game->difficulty <= 0) {
        aiFactor = 0.78f;
    } else if (game->difficulty >= 2) {
        aiFactor = 1.28f;
    }

    game->touchTimer += dt;
    if (game->touchGateActive && !game->touchRiseReached) {
        if ((game->touchStartY - game->ball.pos.y) >= TOUCH_MIN_RISE_PX) {
            game->touchRiseReached = true;
        }
    }
    refresh_difficulty(game);

    if (!game->waitingServe) {
        if (keys[SDL_SCANCODE_A] || (!game->twoPlayerMode && keys[SDL_SCANCODE_LEFT])) {
            game->player.x -= PLAYER_SPEED * dt;
        }
        if (keys[SDL_SCANCODE_D] || (!game->twoPlayerMode && keys[SDL_SCANCODE_RIGHT])) {
            game->player.x += PLAYER_SPEED * dt;
        }
        if (game->twoPlayerMode) {
            if (keys[SDL_SCANCODE_LEFT]) {
                game->cpu.x -= PLAYER_SPEED * dt;
            }
            if (keys[SDL_SCANCODE_RIGHT]) {
                game->cpu.x += PLAYER_SPEED * dt;
            }
        }
    }

    game->player.x = clampf(game->player.x, COURT_MIN_X, NET_X - PLAYER_W - 6.0f);
    game->cpu.x = clampf(game->cpu.x, NET_X + 8.0f, COURT_MAX_X - PLAYER_W);

    playerJumpHeld = keys[SDL_SCANCODE_W] || (!game->twoPlayerMode && keys[SDL_SCANCODE_UP]);
    playerGravityScale = 1.0f;
    if (!game->player.onGround && game->player.vy < 0.0f) {
        if (playerJumpHeld && game->player.jumpHoldTimer > 0.0f) {
            playerGravityScale = PLAYER_JUMP_HOLD_GRAVITY_MULT;
            game->player.jumpHoldTimer -= dt;
            if (game->player.jumpHoldTimer < 0.0f) {
                game->player.jumpHoldTimer = 0.0f;
            }
        } else {
            playerGravityScale = PLAYER_JUMP_RELEASE_GRAVITY_MULT;
        }
    }

    game->player.vy += PLAYER_GRAVITY * playerGravityScale * dt;
    game->player.y += game->player.vy * dt;
    if (game->player.y >= FLOOR_Y - PLAYER_H) {
        game->player.y = FLOOR_Y - PLAYER_H;
        game->player.vy = 0.0f;
        game->player.jumpHoldTimer = 0.0f;
        game->player.onGround = true;
    }

    rightJumpHeld = game->twoPlayerMode && keys[SDL_SCANCODE_UP];
    rightGravityScale = 1.0f;
    if (game->twoPlayerMode && !game->cpu.onGround && game->cpu.vy < 0.0f) {
        if (rightJumpHeld && game->cpu.jumpHoldTimer > 0.0f) {
            rightGravityScale = PLAYER_JUMP_HOLD_GRAVITY_MULT;
            game->cpu.jumpHoldTimer -= dt;
            if (game->cpu.jumpHoldTimer < 0.0f) {
                game->cpu.jumpHoldTimer = 0.0f;
            }
        } else {
            rightGravityScale = PLAYER_JUMP_RELEASE_GRAVITY_MULT;
        }
    }

    game->cpu.vy += PLAYER_GRAVITY * rightGravityScale * dt;
    game->cpu.y += game->cpu.vy * dt;
    if (game->cpu.y >= FLOOR_Y - PLAYER_H) {
        game->cpu.y = FLOOR_Y - PLAYER_H;
        game->cpu.vy = 0.0f;
        game->cpu.jumpHoldTimer = 0.0f;
        game->cpu.onGround = true;
    }

    playerHeadX = game->player.x + PLAYER_W * 0.5f;
    playerHeadY = game->player.y;
    cpuHeadX = game->cpu.x + PLAYER_W * 0.5f;
    cpuHeadY = game->cpu.y;

    compute_arm_pose(game->player.x, game->player.y, true, !game->player.onGround, &playerShoulderX, &playerShoulderY, &playerHandX, &playerHandY);
    compute_arm_pose(game->cpu.x, game->cpu.y, false, !game->cpu.onGround, &cpuShoulderX, &cpuShoulderY, &cpuHandX, &cpuHandY);

    if (game->waitingServe) {
        place_ball_in_server_hand(game);
        if (game->serveCharging) {
            game->serveCharge += dt / SERVE_CHARGE_MAX_TIME;
            game->serveCharge = clampf(game->serveCharge, 0.0f, 1.0f);
        }
        if (!game->twoPlayerMode && game->serverSide > 0) {
            game->cpuServeTimer -= dt;
            if (game->cpuServeTimer <= 0.0f) {
                start_serve(game, 1);
                events |= EVENT_CPU_RETURN;
            }
        }
        return events;
    }

    if (!game->twoPlayerMode) {
        float cpuTargetX = clampf(game->ball.pos.x - (PLAYER_W * 0.5f), NET_X + 8.0f, COURT_MAX_X - PLAYER_W);
        float cpuMoveSpeed = CPU_SPEED * game->cpuSpeedScale * aiFactor;
        if (game->cpu.x < cpuTargetX) {
            game->cpu.x += cpuMoveSpeed * dt;
            if (game->cpu.x > cpuTargetX) {
                game->cpu.x = cpuTargetX;
            }
        } else if (game->cpu.x > cpuTargetX) {
            game->cpu.x -= cpuMoveSpeed * dt;
            if (game->cpu.x < cpuTargetX) {
                game->cpu.x = cpuTargetX;
            }
        }
    }

    if (!game->twoPlayerMode) {
        if (game->ball.pos.x < NET_X + (90.0f * aiFactor) &&
            game->ball.vel.x < -40.0f &&
            game->ball.pos.y > NET_TOP_Y - (40.0f * aiFactor) &&
            game->ball.pos.y < NET_TOP_Y + (110.0f * aiFactor)) {
            shouldCpuBlock = true;
        }

        if (game->ball.pos.x > NET_X + 24.0f &&
            fabsf(game->ball.pos.x - cpuHeadX) < 58.0f * aiFactor &&
            game->ball.pos.y < cpuHeadY - 18.0f * aiFactor &&
            game->ball.vel.y > 40.0f / aiFactor) {
            shouldCpuJump = true;
        }
    }

    if (game->player.isBlocking) {
        game->player.blockTimer -= dt;
        if (game->player.blockTimer <= 0.0f) {
            game->player.isBlocking = false;
            game->player.blockTimer = 0.0f;
        }
    }

    if (game->cpu.isBlocking) {
        game->cpu.blockTimer -= dt;
        if (game->cpu.blockTimer <= 0.0f) {
            game->cpu.isBlocking = false;
            game->cpu.blockTimer = 0.0f;
        }
    } else if (shouldCpuBlock) {
        game->cpu.isBlocking = true;
        game->cpu.blockTimer = CPU_BLOCK_TIME * clampf(aiFactor, 0.8f, 1.35f);
    }

    if (game->cpu.onGround && (shouldCpuJump || shouldCpuBlock)) {
        game->cpu.vy = CPU_JUMP_SPEED;
        game->cpu.jumpHoldTimer = 0.0f;
        game->cpu.onGround = false;
    }

    if (game->cpuHitCooldown > 0.0f) {
        game->cpuHitCooldown -= dt;
        if (game->cpuHitCooldown < 0.0f) {
            game->cpuHitCooldown = 0.0f;
        }
    }

    {
        float prevBallX = game->ball.pos.x;
        float prevBallY = game->ball.pos.y;
        float lowerQuarterY = FLOOR_Y - (FLOOR_Y - 40.0f) * 0.25f;
        bool inLowerQuarter;

        game->ball.vel.y += GRAVITY * dt;
        game->ball.pos.x += game->ball.vel.x * dt;
        game->ball.pos.y += game->ball.vel.y * dt;
        inLowerQuarter = (game->ball.pos.y >= lowerQuarterY);

        if (game->serveOutOnMax && game->ball.pos.x - BALL_RADIUS > COURT_MAX_X) {
            award_point(game, game->lastTouchSide > 0, &events);
            return events;
        }

        if (game->ball.pos.x + BALL_RADIUS < COURT_MIN_X) {
            if (!inLowerQuarter && game->lastTouchSide < 0) {
                game->ball.pos.x = COURT_MIN_X + BALL_RADIUS;
                game->ball.vel.x = fabsf(game->ball.vel.x) * 0.86f;
                events |= EVENT_WALL_BOUNCE;
            } else {
                award_point(game, game->lastTouchSide > 0, &events);
                return events;
            }
        }
        if (game->ball.pos.x - BALL_RADIUS > COURT_MAX_X) {
            if (!inLowerQuarter && game->lastTouchSide > 0) {
                game->ball.pos.x = COURT_MAX_X - BALL_RADIUS;
                game->ball.vel.x = -fabsf(game->ball.vel.x) * 0.86f;
                events |= EVENT_WALL_BOUNCE;
            } else {
                award_point(game, game->lastTouchSide > 0, &events);
                return events;
            }
        }

        if (game->ball.pos.x - BALL_RADIUS < NET_X + 4.0f && game->ball.pos.x + BALL_RADIUS > NET_X - 4.0f && game->ball.pos.y + BALL_RADIUS > NET_TOP_Y) {
            if (game->ball.pos.x < NET_X) {
                game->ball.pos.x = NET_X - BALL_RADIUS - 4.0f;
                game->ball.vel.x = -fabsf(game->ball.vel.x) * 0.86f;
            } else {
                game->ball.pos.x = NET_X + BALL_RADIUS + 4.0f;
                game->ball.vel.x = fabsf(game->ball.vel.x) * 0.86f;
            }
            events |= EVENT_WALL_BOUNCE;
        }

        if (!game->serveOutOnMax && can_touch_ball(game)) {
            bool playerTouched = false;
            if (reflect_ball_on_hand_zone(&game->ball, prevBallX, prevBallY, playerHandX, playerHandY, true, game->player.vy, game->player.onGround)) {
                playerTouched = true;
            } else if (reflect_ball_on_head_zone(&game->ball, prevBallX, prevBallY, playerHeadX, playerHeadY, true, game->player.vy, game->player.onGround)) {
                playerTouched = true;
            }

            if (playerTouched) {
                register_ball_touch(game);
                game->lastTouchSide = -1;
                game->touchesLeft += 1;
                if (game->touchesLeft > MAX_TOUCHES_PER_SIDE) {
                    award_point(game, false, &events);
                    return events;
                }
                events |= EVENT_PLAYER_RECEIVE;
            }
        }

        if (!game->serveOutOnMax && game->ball.pos.x > NET_X + 10.0f) {
            if (can_touch_ball(game)) {
                bool cpuTouched = false;
                if (reflect_ball_on_hand_zone(&game->ball, prevBallX, prevBallY, cpuHandX, cpuHandY, false, game->cpu.vy, game->cpu.onGround)) {
                    cpuTouched = true;
                } else if (reflect_ball_on_head_zone(&game->ball, prevBallX, prevBallY, cpuHeadX, cpuHeadY, false, game->cpu.vy, game->cpu.onGround)) {
                    cpuTouched = true;
                }

                if (cpuTouched) {
                    register_ball_touch(game);
                    game->lastTouchSide = 1;
                    game->touchesRight += 1;
                    if (game->touchesRight > MAX_TOUCHES_PER_SIDE) {
                        award_point(game, true, &events);
                        return events;
                    }
                    if (game->cpuHitCooldown <= 0.0f) {
                        game->cpuHitCooldown = CPU_HIT_COOLDOWN / clampf(game->cpuSpeedScale * aiFactor, 0.6f, 2.8f);
                    }
                    events |= EVENT_CPU_RETURN;
                }
            }
        }

        {
            int currentSide = game->ball.pos.x < NET_X ? -1 : 1;
            if (currentSide != game->ballSide) {
                game->ballSide = currentSide;
                if (currentSide < 0) {
                    game->touchesLeft = 0;
                } else {
                    game->touchesRight = 0;
                }
            }
        }
    }

    if (game->ball.pos.y + BALL_RADIUS >= FLOOR_Y) {
        if (game->ball.pos.x < NET_X) {
            award_point(game, false, &events);
        } else {
            award_point(game, true, &events);
        }
    }

    return events;
}

static void draw_filled_circle(SDL_Renderer *renderer, int cx, int cy, int radius) {
    for (int dy = -radius; dy <= radius; ++dy) {
        int dx = (int)sqrtf((float)(radius * radius - dy * dy));
        SDL_RenderDrawLine(renderer, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}

static void draw_volleyball(SDL_Renderer *renderer, int cx, int cy, int radius) {
    int r2 = radius * radius;

    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            if (x * x + y * y > r2) {
                continue;
            }

            float xf = (float)x / (float)radius;
            float yf = (float)y / (float)radius;

            {
                float seamA = yf - (0.10f + 0.22f * sinf((xf + 0.04f) * 2.4f));
                float seamB = xf - (-0.28f + 0.14f * sinf((yf + 0.07f) * 2.5f));
                float seamC = xf - (0.27f + 0.13f * sinf((yf - 0.03f) * 2.3f));
                bool bluePanel = fabsf(seamA) < 0.15f || fabsf(seamB) < 0.13f || fabsf(seamC) < 0.12f;
                bool bayer = (((x & 1) == 0) && ((y & 1) == 0)) || (((x & 1) != 0) && ((y & 1) != 0));

                if (bluePanel) {
                    if (bayer) {
                        SDL_SetRenderDrawColor(renderer, 62, 114, 188, 255);
                    } else {
                        SDL_SetRenderDrawColor(renderer, 48, 92, 160, 255);
                    }
                } else {
                    if (bayer) {
                        SDL_SetRenderDrawColor(renderer, 236, 194, 108, 255);
                    } else {
                        SDL_SetRenderDrawColor(renderer, 210, 166, 86, 255);
                    }
                }
            }
            SDL_RenderDrawPoint(renderer, cx + x, cy + y);
        }
    }

    SDL_SetRenderDrawColor(renderer, 248, 216, 132, 255);
    for (int a = 0; a < 360; ++a) {
        float rad = (float)a * 3.14159265f / 180.0f;
        int px = cx + (int)((float)radius * cosf(rad));
        int py = cy + (int)((float)radius * sinf(rad));
        SDL_RenderDrawPoint(renderer, px, py);
    }
}


static const uint8_t *glyph_5x7(char c) {
    static const uint8_t SPACE[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    static const uint8_t COLON[7] = {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00};
    static const uint8_t UNDERSCORE[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F};
    static const uint8_t A[7] = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    static const uint8_t B[7] = {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E};
    static const uint8_t C[7] = {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E};
    static const uint8_t D[7] = {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C};
    static const uint8_t E[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
    static const uint8_t F[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10};
    static const uint8_t G[7] = {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E};
    static const uint8_t H[7] = {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    static const uint8_t I[7] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F};
    static const uint8_t J[7] = {0x1F, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C};
    static const uint8_t L[7] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
    static const uint8_t K[7] = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
    static const uint8_t M[7] = {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11};
    static const uint8_t N[7] = {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
    static const uint8_t O[7] = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    static const uint8_t P[7] = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};
    static const uint8_t Q[7] = {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D};
    static const uint8_t R[7] = {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11};
    static const uint8_t S[7] = {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E};
    static const uint8_t T[7] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
    static const uint8_t U[7] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    static const uint8_t V[7] = {0x11, 0x11, 0x11, 0x11, 0x0A, 0x0A, 0x04};
    static const uint8_t W[7] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A};
    static const uint8_t X[7] = {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11};
    static const uint8_t Y[7] = {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04};
    static const uint8_t Z[7] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F};
    static const uint8_t ZERO[7] = {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E};
    static const uint8_t ONE[7] = {0x04, 0x0C, 0x14, 0x04, 0x04, 0x04, 0x1F};
    static const uint8_t TWO[7] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F};
    static const uint8_t THREE[7] = {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E};
    static const uint8_t FOUR[7] = {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02};
    static const uint8_t FIVE[7] = {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E};
    static const uint8_t SIX[7] = {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E};
    static const uint8_t SEVEN[7] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
    static const uint8_t EIGHT[7] = {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E};
    static const uint8_t NINE[7] = {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E};

    switch (c) {
        case 'A': return A;
        case 'B': return B;
        case 'C': return C;
        case 'D': return D;
        case 'E': return E;
        case 'F': return F;
        case 'G': return G;
        case 'H': return H;
        case 'I': return I;
        case 'J': return J;
        case 'K': return K;
        case 'L': return L;
        case 'M': return M;
        case 'N': return N;
        case 'O': return O;
        case 'P': return P;
        case 'Q': return Q;
        case 'R': return R;
        case 'S': return S;
        case 'T': return T;
        case 'U': return U;
        case 'V': return V;
        case 'W': return W;
        case 'X': return X;
        case 'Y': return Y;
        case 'Z': return Z;
        case '0': return ZERO;
        case '1': return ONE;
        case '2': return TWO;
        case '3': return THREE;
        case '4': return FOUR;
        case '5': return FIVE;
        case '6': return SIX;
        case '7': return SEVEN;
        case '8': return EIGHT;
        case '9': return NINE;
        case ':': return COLON;
        case '_': return UNDERSCORE;
        default: return SPACE;
    }
}

static void draw_text_5x7(SDL_Renderer *renderer, int x, int y, const char *text, int scale, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

    for (int i = 0; text[i] != '\0'; ++i) {
        const uint8_t *g = glyph_5x7(text[i]);
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if ((g[row] >> (4 - col)) & 1) {
                    SDL_Rect px = {
                        x + i * (6 * scale) + col * scale,
                        y + row * scale,
                        scale,
                        scale
                    };
                    SDL_RenderFillRect(renderer, &px);
                }
            }
        }
    }
}

static void draw_digit_7seg(SDL_Renderer *renderer, int x, int y, int digit, int scale, SDL_Color color) {
    static const uint8_t DIGIT_MASK[10] = {
        0x3F, 0x06, 0x5B, 0x4F, 0x66,
        0x6D, 0x7D, 0x07, 0x7F, 0x6F
    };
    int thickness = scale;
    int length = 4 * scale;
    uint8_t mask;

    if (digit < 0 || digit > 9) {
        return;
    }

    mask = DIGIT_MASK[digit];
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

    if (mask & (1 << 0)) {
        SDL_Rect seg = {x + thickness, y, length, thickness};
        SDL_RenderFillRect(renderer, &seg);
    }
    if (mask & (1 << 1)) {
        SDL_Rect seg = {x + thickness + length, y + thickness, thickness, length};
        SDL_RenderFillRect(renderer, &seg);
    }
    if (mask & (1 << 2)) {
        SDL_Rect seg = {x + thickness + length, y + 2 * thickness + length, thickness, length};
        SDL_RenderFillRect(renderer, &seg);
    }
    if (mask & (1 << 3)) {
        SDL_Rect seg = {x + thickness, y + 2 * (thickness + length), length, thickness};
        SDL_RenderFillRect(renderer, &seg);
    }
    if (mask & (1 << 4)) {
        SDL_Rect seg = {x, y + 2 * thickness + length, thickness, length};
        SDL_RenderFillRect(renderer, &seg);
    }
    if (mask & (1 << 5)) {
        SDL_Rect seg = {x, y + thickness, thickness, length};
        SDL_RenderFillRect(renderer, &seg);
    }
    if (mask & (1 << 6)) {
        SDL_Rect seg = {x + thickness, y + thickness + length, length, thickness};
        SDL_RenderFillRect(renderer, &seg);
    }
}

static int draw_two_digits_7seg(SDL_Renderer *renderer, int x, int y, int value, int scale, SDL_Color color) {
    int tens;
    int ones;
    int digitWidth;

    if (value < 0) {
        value = 0;
    }
    if (value > 99) {
        value = 99;
    }

    tens = value / 10;
    ones = value % 10;
    digitWidth = 6 * scale;

    draw_digit_7seg(renderer, x, y, tens, scale, color);
    draw_digit_7seg(renderer, x + digitWidth + scale, y, ones, scale, color);
    return digitWidth * 2 + scale;
}

static void render_hud(SDL_Renderer *renderer, const Game *game) {
    int boardW = 300;
    int boardH = 94;
    int boardX = (WINDOW_WIDTH - boardW) / 2;
    int boardY = 12;
    int midX = boardX + boardW / 2;
    int pointY = boardY + 14;
    int setY = boardY + 62;
    int pointGroupW = 52;
    int setGroupW = 26;
    int leftPointX = midX - 88;
    int rightPointX = midX + 40;
    int leftSetX = leftPointX + (pointGroupW - setGroupW) / 2;
    int rightSetX = rightPointX + (pointGroupW - setGroupW) / 2;
    SDL_Color pointColor = {246, 244, 195, 255};
    SDL_Color setColor = {171, 229, 248, 255};
    SDL_Color labelColor = {154, 198, 220, 255};

    SDL_Rect board = {boardX, boardY, boardW, boardH};
    SDL_SetRenderDrawColor(renderer, 8, 18, 28, 220);
    SDL_RenderFillRect(renderer, &board);
    SDL_SetRenderDrawColor(renderer, 62, 120, 156, 255);
    SDL_RenderDrawRect(renderer, &board);

    draw_two_digits_7seg(renderer, leftPointX, pointY, game->playerPoints, 4, pointColor);
    draw_two_digits_7seg(renderer, rightPointX, pointY, game->cpuPoints, 4, pointColor);

    SDL_SetRenderDrawColor(renderer, 154, 198, 220, 255);
    SDL_Rect dotTop = {midX - 2, boardY + 34, 4, 4};
    SDL_Rect dotBottom = {midX - 2, boardY + 46, 4, 4};
    SDL_RenderFillRect(renderer, &dotTop);
    SDL_RenderFillRect(renderer, &dotBottom);

    draw_two_digits_7seg(renderer, leftSetX, setY, game->playerSets, 2, setColor);
    draw_two_digits_7seg(renderer, rightSetX, setY, game->cpuSets, 2, setColor);
    draw_text_5x7(renderer, midX - 12, boardY + 80, "SETS", 1, labelColor);
}

static void render_scene_overlay(
    SDL_Renderer *renderer,
    Scene scene,
    float uiTimeSeconds,
    bool startTwoPlayer,
    bool audioMuted,
    int startDifficulty,
    const HighscoreEntry *highscores,
    int highscoreCount,
    const char *nameInput,
    int nameSetsFor,
    int nameSetsAgainst,
    int namePointsFor,
    int namePointsAgainst
) {
    if (scene == SCENE_PLAYING) {
        return;
    }

    SDL_Rect box = {200, 182, 560, 280};
    if (scene == SCENE_START) {
        box.x = 96;
        box.y = 238;
    }
    SDL_SetRenderDrawColor(renderer, 6, 12, 20, 230);
    SDL_RenderFillRect(renderer, &box);
    SDL_SetRenderDrawColor(renderer, 71, 132, 163, 255);
    SDL_RenderDrawRect(renderer, &box);

    SDL_Color title = {245, 230, 165, 255};
    SDL_Color text = {176, 214, 230, 255};

    if (scene == SCENE_START) {
        bool blink = fmodf(uiTimeSeconds, 1.0f) < 0.58f;
        const char *difficultyLabel = "NORMAL";

        if (startDifficulty <= 0) {
            difficultyLabel = "EASY";
        } else if (startDifficulty >= 2) {
            difficultyLabel = "HARD";
        }

        SDL_SetRenderDrawColor(renderer, 54, 96, 124, 255);
        SDL_RenderDrawLine(renderer, box.x + 28, box.y + 92, box.x + 212, box.y + 92);

        draw_text_5x7(renderer, box.x + 22, box.y + 42, "START", 4, title);
        if (blink) {
            draw_text_5x7(renderer, box.x + 78, box.y + 112, "PRESS ENTER", 2, text);
        }
        if (startTwoPlayer) {
            draw_text_5x7(renderer, box.x + 78, box.y + 136, "PLAYER TWO", 2, text);
        } else {
            draw_text_5x7(renderer, box.x + 78, box.y + 136, "PLAYER ONE", 2, text);
        }
        draw_text_5x7(renderer, box.x + 102, box.y + 152, "UP DOWN", 1, text);

        draw_text_5x7(renderer, box.x + 78, box.y + 164, "DIFF", 2, text);
        draw_text_5x7(renderer, box.x + 156, box.y + 164, difficultyLabel, 2, text);
        draw_text_5x7(renderer, box.x + 92, box.y + 180, "LEFT RIGHT", 1, text);

        if (audioMuted) {
            draw_text_5x7(renderer, box.x + 78, box.y + 192, "SOUND MUTE", 2, text);
        } else {
            draw_text_5x7(renderer, box.x + 78, box.y + 192, "SOUND LIVE", 2, text);
        }
        draw_text_5x7(renderer, box.x + 102, box.y + 208, "SPACE", 1, text);
        draw_text_5x7(renderer, box.x + 78, box.y + 220, "ENTER PLAY  ESC", 1, text);

        if (!startTwoPlayer) {
            draw_text_5x7(renderer, box.x + 336, box.y + 134, "HIGHSCORES", 1, text);
            for (int i = 0; i < highscoreCount && i < MAX_HIGHSCORES; ++i) {
                char line[64];
                snprintf(
                    line,
                    sizeof(line),
                    "%d %.10s S%d:%d P%d:%d",
                    i + 1,
                    highscores[i].name,
                    highscores[i].setsFor,
                    highscores[i].setsAgainst,
                    highscores[i].pointsFor,
                    highscores[i].pointsAgainst
                );
                draw_text_5x7(renderer, box.x + 336, box.y + 146 + i * 12, line, 1, text);
            }
        }
    } else if (scene == SCENE_PAUSED) {
        draw_text_5x7(renderer, box.x + 126, box.y + 42, "PAUSE", 4, title);
        draw_text_5x7(renderer, box.x + 84, box.y + 122, "P OR ENTER TO RESUME", 2, text);
        draw_text_5x7(renderer, box.x + 88, box.y + 162, "W BLOCK  R RESET", 1, text);
    } else if (scene == SCENE_GAME_OVER) {
        SDL_SetRenderDrawColor(renderer, 128, 48, 58, 255);
        SDL_RenderDrawRect(renderer, &box);
        draw_text_5x7(renderer, box.x + 74, box.y + 42, "GAME OVER", 4, title);
        draw_text_5x7(renderer, box.x + 84, box.y + 122, "PRESS ENTER RETRY", 2, text);
        draw_text_5x7(renderer, box.x + 84, box.y + 162, "R TO TITLE  M SOUND", 1, text);
    } else if (scene == SCENE_NAME_ENTRY) {
        char scoreLine[32];
        char nameLine[48];
        bool blink = fmodf(uiTimeSeconds, 1.0f) < 0.58f;

        snprintf(
            scoreLine,
            sizeof(scoreLine),
            "SETS %d:%d POINTS %d:%d",
            nameSetsFor,
            nameSetsAgainst,
            namePointsFor,
            namePointsAgainst
        );
        if (blink) {
            snprintf(nameLine, sizeof(nameLine), "NAME %s_", nameInput);
        } else {
            snprintf(nameLine, sizeof(nameLine), "NAME %s", nameInput);
        }

        draw_text_5x7(renderer, box.x + 66, box.y + 42, "NEW HIGHSCORE", 3, title);
        draw_text_5x7(renderer, box.x + 88, box.y + 122, scoreLine, 2, text);
        draw_text_5x7(renderer, box.x + 88, box.y + 154, nameLine, 2, text);
        draw_text_5x7(renderer, box.x + 90, box.y + 186, "TYPE NAME ENTER SAVE", 1, text);
        draw_text_5x7(renderer, box.x + 90, box.y + 202, "BACKSPACE DELETE", 1, text);
        draw_text_5x7(renderer, box.x + 90, box.y + 218, "ESC CANCEL", 1, text);
    }
}

static void render_game(
    SDL_Renderer *renderer,
    const Game *game,
    Scene scene,
    float uiTimeSeconds,
    bool startTwoPlayer,
    bool audioMuted,
    int startDifficulty,
    const HighscoreEntry *highscores,
    int highscoreCount,
    const char *nameInput,
    int nameSetsFor,
    int nameSetsAgainst,
    int namePointsFor,
    int namePointsAgainst
) {
    SDL_SetRenderDrawColor(renderer, 14, 25, 38, 255);
    SDL_RenderClear(renderer);

    SDL_Rect court = {(int)COURT_MIN_X, 70, (int)(COURT_MAX_X - COURT_MIN_X), (int)(FLOOR_Y - 70.0f)};
    SDL_SetRenderDrawColor(renderer, 21, 48, 70, 255);
    SDL_RenderFillRect(renderer, &court);

    {
        SDL_Rect banner = {(WINDOW_WIDTH - 392) / 2, 164, 392, 58};
        int tipW = 24;
        int midY = banner.y + banner.h / 2;
        const char *bannerLabel = "VOLLEY ARCADE";
        int textScale = 3;
        int textWidth = ((int)strlen(bannerLabel) * 6 - 1) * textScale;
        int textHeight = 7 * textScale;
        int textX = banner.x + (banner.w - textWidth) / 2;
        int textY = banner.y + (banner.h - textHeight) / 2;
        SDL_Color bannerText = {250, 238, 170, 255};
        SDL_Color bannerShadow = {24, 44, 66, 255};

        SDL_SetRenderDrawColor(renderer, 24, 86, 122, 255);
        SDL_RenderFillRect(renderer, &banner);

        for (int y = 0; y < banner.h; ++y) {
            int dy = abs(y - banner.h / 2);
            int inset = (dy * tipW) / (banner.h / 2);
            int lx = banner.x - tipW + inset;
            int rx = banner.x + banner.w + tipW - inset;

            SDL_RenderDrawLine(renderer, lx, banner.y + y, banner.x - 1, banner.y + y);
            SDL_RenderDrawLine(renderer, banner.x + banner.w, banner.y + y, rx, banner.y + y);
        }

        SDL_SetRenderDrawColor(renderer, 236, 196, 106, 255);
        SDL_RenderDrawRect(renderer, &banner);
        SDL_RenderDrawLine(renderer, banner.x - tipW, midY, banner.x, banner.y);
        SDL_RenderDrawLine(renderer, banner.x - tipW, midY, banner.x, banner.y + banner.h - 1);
        SDL_RenderDrawLine(renderer, banner.x + banner.w + tipW, midY, banner.x + banner.w - 1, banner.y);
        SDL_RenderDrawLine(renderer, banner.x + banner.w + tipW, midY, banner.x + banner.w - 1, banner.y + banner.h - 1);

        draw_text_5x7(renderer, textX + 1, textY + 1, bannerLabel, textScale, bannerShadow);
        draw_text_5x7(renderer, textX, textY, bannerLabel, textScale, bannerText);
    }

    SDL_SetRenderDrawColor(renderer, 224, 196, 122, 255);
    SDL_RenderDrawLine(renderer, (int)COURT_MIN_X, (int)FLOOR_Y - 1, (int)COURT_MAX_X, (int)FLOOR_Y - 1);
    SDL_RenderDrawLine(renderer, (int)COURT_MIN_X, (int)FLOOR_Y, (int)COURT_MAX_X, (int)FLOOR_Y);
    SDL_RenderDrawLine(renderer, (int)COURT_MIN_X, (int)FLOOR_Y + 1, (int)COURT_MAX_X, (int)FLOOR_Y + 1);

    {
        int leftPoleX = (int)NET_X - 14;
        int rightPoleX = (int)NET_X + 24;
        int meshLeftTopX = leftPoleX + 2;
        int meshLeftTopY = (int)NET_TOP_Y + 8;
        int meshRightTopX = rightPoleX - 2;
        int meshRightTopY = (int)NET_TOP_Y + 14;

        int meshLeftBottomX = leftPoleX + 8;
        int meshLeftBottomY = meshLeftTopY + 64;
        int meshRightBottomX = rightPoleX - 6;
        int meshRightBottomY = meshRightTopY + 68;

        int leftTopY = meshLeftTopY - 8;
        int rightTopY = meshRightTopY - 8;
        int leftBottomY = (int)FLOOR_Y;
        int rightBottomY = (int)FLOOR_Y;

        SDL_SetRenderDrawColor(renderer, 176, 176, 176, 255);
        SDL_Rect leftPole = {leftPoleX - 1, leftTopY, 3, leftBottomY - leftTopY + 1};
        SDL_Rect rightPole = {rightPoleX - 1, rightTopY, 3, rightBottomY - rightTopY + 1};
        SDL_RenderFillRect(renderer, &leftPole);
        SDL_RenderFillRect(renderer, &rightPole);

        SDL_SetRenderDrawColor(renderer, 245, 245, 245, 255);
        SDL_RenderDrawLine(renderer, meshLeftTopX, meshLeftTopY, meshRightTopX, meshRightTopY);
        SDL_RenderDrawLine(renderer, meshLeftTopX, meshLeftTopY + 1, meshRightTopX, meshRightTopY + 1);
        SDL_RenderDrawLine(renderer, meshLeftTopX, meshLeftTopY + 2, meshRightTopX, meshRightTopY + 2);
        SDL_RenderDrawLine(renderer, meshLeftBottomX, meshLeftBottomY, meshRightBottomX, meshRightBottomY);
        SDL_RenderDrawLine(renderer, meshLeftBottomX, meshLeftBottomY - 1, meshRightBottomX, meshRightBottomY - 1);
        SDL_RenderDrawLine(renderer, meshLeftBottomX, meshLeftBottomY - 2, meshRightBottomX, meshRightBottomY - 2);

        SDL_SetRenderDrawColor(renderer, 48, 48, 48, 255);
        for (int i = 0; i <= 8; ++i) {
            float t = (float)i / 8.0f;
            int xTop = (int)((1.0f - t) * (float)meshLeftTopX + t * (float)meshRightTopX);
            int yTop = (int)((1.0f - t) * (float)meshLeftTopY + t * (float)meshRightTopY);
            int xBottom = (int)((1.0f - t) * (float)meshLeftBottomX + t * (float)meshRightBottomX);
            int yBottom = (int)((1.0f - t) * (float)meshLeftBottomY + t * (float)meshRightBottomY);
            SDL_RenderDrawLine(renderer, xTop, yTop, xBottom, yBottom);
        }

        for (int j = 0; j <= 6; ++j) {
            float t = (float)j / 6.0f;
            int xLeft = (int)((1.0f - t) * (float)meshLeftTopX + t * (float)meshLeftBottomX);
            int yLeft = (int)((1.0f - t) * (float)meshLeftTopY + t * (float)meshLeftBottomY);
            int xRight = (int)((1.0f - t) * (float)meshRightTopX + t * (float)meshRightBottomX);
            int yRight = (int)((1.0f - t) * (float)meshRightTopY + t * (float)meshRightBottomY);
            SDL_RenderDrawLine(renderer, xLeft, yLeft, xRight, yRight);
        }
    }

    SDL_Rect player = {(int)game->player.x, (int)game->player.y, (int)PLAYER_W, (int)PLAYER_H};
    float playerShoulderX;
    float playerShoulderY;
    float playerHandX;
    float playerHandY;
    float cpuShoulderX;
    float cpuShoulderY;
    float cpuHandX;
    float cpuHandY;
    bool playerArmInFront;
    bool cpuArmInFront;
    SDL_Color playerBodyColor;
    SDL_Color cpuBodyColor;
    SDL_Color handColor = {255, 188, 110, 255};

    if (game->player.isBlocking) {
        playerBodyColor = (SDL_Color){255, 206, 84, 255};
        SDL_SetRenderDrawColor(renderer, 255, 206, 84, 255);
    } else {
        playerBodyColor = (SDL_Color){228, 96, 132, 255};
        SDL_SetRenderDrawColor(renderer, 228, 96, 132, 255);
    }
    SDL_RenderFillRect(renderer, &player);

    SDL_Rect cpu = {(int)game->cpu.x, (int)game->cpu.y, (int)PLAYER_W, (int)PLAYER_H};
    if (game->cpu.isBlocking) {
        cpuBodyColor = (SDL_Color){255, 206, 84, 255};
        SDL_SetRenderDrawColor(renderer, 255, 206, 84, 255);
    } else {
        cpuBodyColor = (SDL_Color){130, 240, 152, 255};
        SDL_SetRenderDrawColor(renderer, 130, 240, 152, 255);
    }
    SDL_RenderFillRect(renderer, &cpu);

    compute_arm_pose(game->player.x, game->player.y, true, !game->player.onGround, &playerShoulderX, &playerShoulderY, &playerHandX, &playerHandY);
    compute_arm_pose(game->cpu.x, game->cpu.y, false, !game->cpu.onGround, &cpuShoulderX, &cpuShoulderY, &cpuHandX, &cpuHandY);
    playerArmInFront = !game->player.onGround;
    cpuArmInFront = !game->cpu.onGround;

    if (!playerArmInFront) {
        draw_arm_with_hand(renderer, playerShoulderX, playerShoulderY, playerHandX, playerHandY, true, playerBodyColor, handColor);
    }
    if (!cpuArmInFront) {
        draw_arm_with_hand(renderer, cpuShoulderX, cpuShoulderY, cpuHandX, cpuHandY, false, cpuBodyColor, handColor);
    }

    SDL_SetRenderDrawColor(renderer, 255, 188, 110, 255);
    draw_filled_circle(
        renderer,
        (int)(game->player.x + PLAYER_W * 0.5f),
        (int)game->player.y,
        (int)HEAD_RADIUS
    );
    draw_filled_circle(
        renderer,
        (int)(game->cpu.x + PLAYER_W * 0.5f),
        (int)game->cpu.y,
        (int)HEAD_RADIUS
    );

    if (playerArmInFront) {
        draw_arm_with_hand(renderer, playerShoulderX, playerShoulderY, playerHandX, playerHandY, true, playerBodyColor, handColor);
    }
    if (cpuArmInFront) {
        draw_arm_with_hand(renderer, cpuShoulderX, cpuShoulderY, cpuHandX, cpuHandY, false, cpuBodyColor, handColor);
    }

    if (game->waitingServe && (game->serverSide < 0 || game->twoPlayerMode)) {
        int bx;
        int by;
        int bw = 56;
        int bh = 8;
        int fill = (int)((float)(bw - 2) * game->serveCharge);
        SDL_Rect bg;
        SDL_Rect fg;

        if (game->serverSide < 0) {
            bx = (int)(game->player.x + PLAYER_W * 0.5f) - 28;
            by = (int)(game->player.y - HEAD_RADIUS - BALL_RADIUS - 20.0f);
        } else {
            bx = (int)(game->cpu.x + PLAYER_W * 0.5f) - 28;
            by = (int)(game->cpu.y - HEAD_RADIUS - BALL_RADIUS - 20.0f);
        }

        bg.x = bx;
        bg.y = by;
        bg.w = bw;
        bg.h = bh;
        fg.x = bx + 1;
        fg.y = by + 1;
        fg.w = fill;
        fg.h = bh - 2;

        SDL_SetRenderDrawColor(renderer, 20, 32, 44, 255);
        SDL_RenderFillRect(renderer, &bg);

        if (game->serveCharge >= 0.999f && (fmodf(uiTimeSeconds * 10.0f, 2.0f) < 1.0f)) {
            SDL_SetRenderDrawColor(renderer, 240, 96, 82, 255);
        } else {
            SDL_SetRenderDrawColor(renderer, 86, 214, 176, 255);
        }
        SDL_RenderFillRect(renderer, &fg);
        SDL_SetRenderDrawColor(renderer, 145, 189, 212, 255);
        SDL_RenderDrawRect(renderer, &bg);
    }

    render_hud(renderer, game);
    draw_volleyball(renderer, (int)game->ball.pos.x, (int)game->ball.pos.y, (int)BALL_RADIUS);
    render_scene_overlay(
        renderer,
        scene,
        uiTimeSeconds,
        startTwoPlayer,
        audioMuted,
        startDifficulty,
        highscores,
        highscoreCount,
        nameInput,
        nameSetsFor,
        nameSetsAgainst,
        namePointsFor,
        namePointsAgainst
    );

    SDL_RenderPresent(renderer);
}

int main(void) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    Audio audio = {0};
    {
        SDL_AudioSpec wanted;
        SDL_zero(wanted);
        wanted.freq = AUDIO_SAMPLE_RATE;
        wanted.format = AUDIO_S16SYS;
        wanted.channels = 1;
        wanted.samples = 512;
        wanted.callback = NULL;

        audio.device = SDL_OpenAudioDevice(NULL, 0, &wanted, &audio.spec, 0);
        if (audio.device != 0) {
            SDL_PauseAudioDevice(audio.device, 0);
        }
    }

    SDL_Window *window = SDL_CreateWindow(
        "Volley-Arcade",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        WINDOW_WIDTH,
        WINDOW_HEIGHT,
        SDL_WINDOW_SHOWN
    );
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        if (audio.device != 0) {
            SDL_CloseAudioDevice(audio.device);
        }
        SDL_Quit();
        return 1;
    }

    bool startTwoPlayer = false;
    int startDifficulty = 1;
    HighscoreEntry highscores[MAX_HIGHSCORES];
    int highscoreCount = load_highscores(highscores, MAX_HIGHSCORES);
    char lastPlayerName[HIGHSCORE_NAME_LEN + 1] = "PLAYER";
    char nameInput[HIGHSCORE_NAME_LEN + 1] = "PLAYER";
    int nameInputLen = 6;
    int nameSetsFor = 0;
    int nameSetsAgainst = 0;
    int namePointsFor = 0;
    int namePointsAgainst = 0;
    Game game;
    init_game(&game, startTwoPlayer, startDifficulty);
    Scene scene = SCENE_START;

    bool running = true;
    Uint64 lastCounter = SDL_GetPerformanceCounter();
    float accumulator = 0.0f;
    float uiTimeSeconds = 0.0f;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
            if (event.type == SDL_KEYDOWN) {
                SDL_Scancode sc = event.key.keysym.scancode;

                if (scene == SCENE_NAME_ENTRY) {
                    if (sc == SDL_SCANCODE_BACKSPACE) {
                        if (nameInputLen > 0) {
                            nameInputLen -= 1;
                            nameInput[nameInputLen] = '\0';
                        }
                        continue;
                    }
                    if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER) {
                        if (nameInputLen == 0) {
                            snprintf(nameInput, sizeof(nameInput), "%s", lastPlayerName);
                            nameInputLen = (int)strlen(nameInput);
                        }
                        snprintf(lastPlayerName, sizeof(lastPlayerName), "%s", nameInput);
                        insert_highscore(
                            highscores,
                            &highscoreCount,
                            nameInput,
                            nameSetsFor,
                            nameSetsAgainst,
                            namePointsFor,
                            namePointsAgainst
                        );
                        save_highscores(highscores, highscoreCount);
                        scene = SCENE_START;
                        continue;
                    }
                    if (sc == SDL_SCANCODE_ESCAPE) {
                        scene = SCENE_START;
                        continue;
                    }

                    {
                        char ch = scancode_to_name_char(sc);
                        if (ch != '\0' && nameInputLen < HIGHSCORE_NAME_LEN) {
                            nameInput[nameInputLen++] = ch;
                            nameInput[nameInputLen] = '\0';
                        }
                    }
                    continue;
                }

                if (scene == SCENE_START) {
                    if (sc == SDL_SCANCODE_UP) {
                        startTwoPlayer = false;
                    }
                    if (sc == SDL_SCANCODE_DOWN) {
                        startTwoPlayer = true;
                    }
                    if (sc == SDL_SCANCODE_LEFT) {
                        if (startDifficulty > 0) {
                            startDifficulty -= 1;
                        }
                    }
                    if (sc == SDL_SCANCODE_RIGHT) {
                        if (startDifficulty < 2) {
                            startDifficulty += 1;
                        }
                    }
                    if (sc == SDL_SCANCODE_SPACE) {
                        audio.muted = !audio.muted;
                        if (audio.muted && audio.device != 0) {
                            SDL_ClearQueuedAudio(audio.device);
                        }
                    }
                }

                if ((sc == SDL_SCANCODE_W || (!game.twoPlayerMode && sc == SDL_SCANCODE_UP)) && scene == SCENE_PLAYING) {
                    if (game.player.onGround) {
                        game.player.vy = PLAYER_JUMP_SPEED;
                        game.player.jumpHoldTimer = PLAYER_JUMP_HOLD_TIME;
                        game.player.onGround = false;
                    }
                    if (!game.waitingServe) {
                        game.player.isBlocking = true;
                        game.player.blockTimer = game.blockWindow;
                    }
                }

                if (sc == SDL_SCANCODE_UP && scene == SCENE_PLAYING && game.twoPlayerMode) {
                    if (game.cpu.onGround) {
                        game.cpu.vy = PLAYER_JUMP_SPEED;
                        game.cpu.jumpHoldTimer = PLAYER_JUMP_HOLD_TIME;
                        game.cpu.onGround = false;
                    }
                    if (!game.waitingServe) {
                        game.cpu.isBlocking = true;
                        game.cpu.blockTimer = game.blockWindow;
                    }
                }

                if ((sc == SDL_SCANCODE_LCTRL || (!game.twoPlayerMode && sc == SDL_SCANCODE_RCTRL)) && scene == SCENE_PLAYING) {
                    if (game.waitingServe && game.serverSide < 0) {
                        game.serveCharging = true;
                    }
                }

                if ((sc == SDL_SCANCODE_RALT || sc == SDL_SCANCODE_MODE) && scene == SCENE_PLAYING && game.twoPlayerMode) {
                    if (game.waitingServe && game.serverSide > 0) {
                        game.serveCharging = true;
                    }
                }

                if (sc == SDL_SCANCODE_R) {
                    init_game(&game, startTwoPlayer, startDifficulty);
                    scene = SCENE_START;
                }

                if (sc == SDL_SCANCODE_M) {
                    audio.muted = !audio.muted;
                    if (audio.muted && audio.device != 0) {
                        SDL_ClearQueuedAudio(audio.device);
                    }
                }

                if (sc == SDL_SCANCODE_P) {
                    if (scene == SCENE_PLAYING) {
                        scene = SCENE_PAUSED;
                    } else if (scene == SCENE_PAUSED) {
                        scene = SCENE_PLAYING;
                    }
                }

                if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER) {
                    if (scene == SCENE_START) {
                        init_game(&game, startTwoPlayer, startDifficulty);
                        scene = SCENE_PLAYING;
                    } else if (scene == SCENE_PAUSED) {
                        scene = SCENE_PLAYING;
                    } else if (scene == SCENE_GAME_OVER) {
                        init_game(&game, startTwoPlayer, startDifficulty);
                        scene = SCENE_PLAYING;
                    }
                }

                if (sc == SDL_SCANCODE_ESCAPE) {
                    if (scene == SCENE_PLAYING && !game.twoPlayerMode) {
                        scene = SCENE_NAME_ENTRY;
                        nameSetsFor = game.playerSets;
                        nameSetsAgainst = game.cpuSets;
                        namePointsFor = game.playerPoints;
                        namePointsAgainst = game.cpuPoints;
                        snprintf(nameInput, sizeof(nameInput), "%s", lastPlayerName);
                        nameInputLen = (int)strlen(nameInput);
                    } else if (scene == SCENE_START) {
                        running = false;
                    } else {
                        scene = SCENE_START;
                    }
                }
            }

            if (event.type == SDL_KEYUP) {
                SDL_Scancode sc = event.key.keysym.scancode;
                if (sc == SDL_SCANCODE_W || (!game.twoPlayerMode && sc == SDL_SCANCODE_UP)) {
                    game.player.jumpHoldTimer = 0.0f;
                }
                if (game.twoPlayerMode && sc == SDL_SCANCODE_UP) {
                    game.cpu.jumpHoldTimer = 0.0f;
                }
                if ((sc == SDL_SCANCODE_LCTRL || (!game.twoPlayerMode && sc == SDL_SCANCODE_RCTRL)) && scene == SCENE_PLAYING) {
                    if (game.waitingServe && game.serverSide < 0 && game.serveCharging) {
                        start_human_charged_serve(&game, -1);
                    }
                }
                if ((sc == SDL_SCANCODE_RALT || sc == SDL_SCANCODE_MODE) && scene == SCENE_PLAYING && game.twoPlayerMode) {
                    if (game.waitingServe && game.serverSide > 0 && game.serveCharging) {
                        start_human_charged_serve(&game, 1);
                    }
                }
            }
        }

        Uint64 currentCounter = SDL_GetPerformanceCounter();
        float frameTime = (float)(currentCounter - lastCounter) / (float)SDL_GetPerformanceFrequency();
        if (frameTime > MAX_ACCUMULATED_TIME) {
            frameTime = MAX_ACCUMULATED_TIME;
        }
        lastCounter = currentCounter;
        accumulator += frameTime;
        uiTimeSeconds += frameTime;

        const Uint8 *keys = SDL_GetKeyboardState(NULL);

        while (accumulator >= FIXED_DT) {
            if (scene == SCENE_PLAYING) {
                unsigned events = update_game(&game, FIXED_DT, keys);
                play_events(&audio, events);
                if ((events & EVENT_GAME_OVER) && !game.twoPlayerMode) {
                    scene = SCENE_NAME_ENTRY;
                    nameSetsFor = game.playerSets;
                    nameSetsAgainst = game.cpuSets;
                    namePointsFor = game.playerPoints;
                    namePointsAgainst = game.cpuPoints;
                    snprintf(nameInput, sizeof(nameInput), "%s", lastPlayerName);
                    nameInputLen = (int)strlen(nameInput);
                    break;
                }
            } else {
                accumulator = 0.0f;
                break;
            }
            accumulator -= FIXED_DT;
        }

        render_game(
            renderer,
            &game,
            scene,
            uiTimeSeconds,
            startTwoPlayer,
            audio.muted,
            startDifficulty,
            highscores,
            highscoreCount,
            nameInput,
            nameSetsFor,
            nameSetsAgainst,
            namePointsFor,
            namePointsAgainst
        );

        SDL_SetWindowTitle(window, "Volley-Arcade");
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    if (audio.device != 0) {
        SDL_ClearQueuedAudio(audio.device);
        SDL_CloseAudioDevice(audio.device);
    }
    SDL_Quit();
    return 0;
}
