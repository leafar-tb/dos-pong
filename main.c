asm (".code16gcc;"
     "call  dosmain;"
     "mov   $0x4C, %ah;"
     "int   $0x21;");

typedef unsigned char byte;
typedef unsigned short ushort;
typedef ushort size_t;
typedef unsigned int uint;
typedef byte bool;

#define true 1
#define false 0

// gcc doesn't allow clobbering input registers
// so we just do an empty asm for clobbering those
// (original asm probably needs to be volatile)
#define CLOBBERED(...) asm volatile ( "" : : : __VA_ARGS__)

//###################################################

static void print(char *string) {
    asm volatile (
        "mov   $0x09, %%ah;"
        "int   $0x21;"
        : : "d"(string) : "ah"
    );
}

static char readASCII_blocking() {
    char result;
    asm volatile (
        // read keyboard input
        "mov   $0, %%ah;"
        "int   $0x16;"
        // al=ascii code; ah=scan code
        "mov %%al, %0;"
        : "=r"(result) : : "ax"
    );
    return result;
}

static bool keyInputAvailable() {
    bool result;
    asm volatile (
        // read keyboard status; clears zero-flag, if char in keyboard buffer
        "mov   $0x11, %%ah;"
        "int   $0x16;"
        "jnz 0f;"
            "mov $0, %0;"
            "jmp 1f;"
        "\n 0:\n"
            "mov $1, %0;"
        "\n 1:\n"
        : "=r"(result) : : "ax"
    );
    return result;
}

// returns 0 if no character ready
static char readASCII() {
    char result;
    asm volatile (
        // read keyboard status; clears zero-flag, if char in keyboard buffer
        "mov   $0x11, %%ah;"
        "int   $0x16;"
        "jnz 1f;"
            "mov $0, %0;" // no char -> set 0
            "jmp 2f;"
        "\n 1: \n"
            // read keyboard input
            "mov   $0, %%ah;"
            "int   $0x16;"
            // al=ascii code; ah=scan code
            "mov %%al, %0;"
        "\n 2: \n"
        : "=r"(result) : : "ax"
    );
    return result;
}

//###################################################

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 200

// segments 0 and 1 are in use by bios and our program
// so we use segment 2 for our double-buffering
//static const byte* offscreenBuffer = (const byte*) 0x20000;

static void modeVGA256() {
    asm volatile ( //0x00: set mode; 0x13: vga 256
        "mov   $0x0013, %%ax;"
        "int   $0x10;" // BIOS video
        : : : "ax"
    );
    // init %%es to off-screen buffer
    // init %%fs to VGA segment
    asm volatile (
        "mov $0x2000, %%ax;"
        "mov %%ax, %%es;"
        "mov $0xA000, %%ax;"
        "mov %%ax, %%fs;"
        : : : "%ax"
    );
}

static void modeText() {
    asm volatile ( //0x00: set mode; 0x03: text
        "mov   $0x0003, %%ax;"
        "int   $0x10;"
        : : : "ax"
    );
}

//###################################################

static void waitMillis(ushort millis) {
    asm volatile (
        "mov $1000, %%cx;"
        "mul  %%cx;" // DX:AX = millis*1000
        "mov  %%dx, %%cx;"
        "mov  %%ax, %%dx;"
        // wait for CX:DX microseconds
        "mov  $0x86, %%ah;"
        "int  $0x15;"
        : : "a"(millis) :
    );
    CLOBBERED("ax", "cx", "dx");
}

//###################################################

typedef struct {
    short x;
    short y;
} Point;

Point addPoints(Point a, Point b) {
    return (Point){a.x+b.x, a.y+b.y};
}
void addPoints_ip(Point* a, Point b) {
    a->x += b.x;
    a->y += b.y;
}

//###################################################

//byte offscreenBuffer[SCREEN_HEIGHT*SCREEN_WIDTH];

static void setPixel(short x, short y, byte colour) {
    asm volatile (
        "mov %1, %%es:(%%bx);"
        : : "b"(x+y*320), "rl"(colour) :
    );
}

static void fillArea(short leftx, short topy, ushort width, ushort height, byte colour) {
    for(short y = topy;  y < topy+height; ++y) {
        asm volatile (
            "cld;"
            "rep"
            " stosb;"
            : : "c"(width), "D"((short)(leftx+y*320)), "a"(colour) :
        );
        CLOBBERED("%cx", "%di");
    }
}

static void clearScreenBlack() {
    asm volatile ( // seems to be aimed at text mode, but works for video too
        "mov $0x0600, %%ax;" // 0x06= scroll up window; 0=clear window
        "mov 1, %%bx;" // attribute byte (?)
        "mov $0, %%cx;" // top row/col = 0/0
        "mov $0xC8FF, %%dx;" // bot row/col = 200/256
        "int $0x10;"
        : : : "ax", "bx", "cx", "dx"
    );
}

static void clearScreen(byte colour) {
    uint col = ( colour << 8 ) | colour;
    col |= col << 16;
    asm volatile (
        "mov $16000, %%ecx;"
        "mov $0, %%edi;"
        "cld;"
        "rep"
        " stosl;"
        : : "a"(col) : "%ecx", "%edi", "memory"
    );
}

static void vsync() {
    asm volatile (
        "1: "
        "mov $0x3DA, %%dx;"
        "in %%dx, %%al;" // vga register
        "and $0x8, %%al;" // bit 3 indicates vertical retrace
        "jz 1b;"
        : : : "ax", "dx"
    );
}

static void flipBuffers() {
    asm volatile (
        "push %%ds;"
        "mov %%es, %%ax;" // src = offscreen buffer
        "mov %%ax, %%ds;"
        "mov %%fs, %%ax;" // dest = screen buffer
        "mov %%ax, %%es;"

        "mov $16000, %%ecx;"
        "mov $0, %%edi;"
        "mov $0, %%esi;"
        "cld;"
        "rep"
        " movsl;"

        // restore segments
        "mov %%es, %%ax;"
        "mov %%ax, %%fs;"
        "mov %%ds, %%ax;"
        "mov %%ax, %%es;"
        "pop %%ds;"
        : : : "%ax", "%ecx", "%edi", "%esi", "memory"
    );
}

//###################################################

// from https://github.com/nothings/

static uint stb__rand_seed=0;

uint stb_srandLCG(unsigned long seed) {
   uint previous = stb__rand_seed;
   stb__rand_seed = seed;
   return previous;
}

uint stb_randLCG(void) {
   stb__rand_seed = stb__rand_seed * 2147001325 + 715136305; // BCPL generator
   // shuffle non-random bits to the middle, and xor to decorrelate with seed
   return 0x31415926 ^ ((stb__rand_seed >> 16) + (stb__rand_seed << 16));
}

//#########################

// 0 or 1
bool coinFlip() {
    return stb_randLCG() & 1;
}

// +1 or -1
short rsign() {
    return (coinFlip() << 1) - 1;
}

short sign(short in) {
    short retval;
    asm (
        "cwd;"
        : "=d"(retval) : "a"(in) :
    );
    return (retval << 1) + 1;
}

ushort abs(short in) {
    ushort retval;
    asm (
        "cwd;"
        "xor %%dx, %%ax;"
        "sub %%dx, %%ax;"
        : "=a"(retval) : "a"(in) : "%dx"
    );
    return retval;
}

//###################################################

// positions store topleft corner

Point ballPos;
#define BALL_DIM 3
Point ballSpeed;

Point paddleLeftPos, paddleRightPos;
#define PADDLE_WIDTH 5
#define PADDLE_HEIGHT 20

static const byte MAX_SCORE = 3;
byte scoreLeft, scoreRight;

//###################################################

void newRound() {
    ballPos = (Point){SCREEN_WIDTH/2, SCREEN_HEIGHT/2};
    ballSpeed = (Point){rsign(), rsign()};
}

void newGame() {
    newRound();

    paddleLeftPos = (Point){50, SCREEN_HEIGHT/2};
    paddleRightPos = (Point){SCREEN_WIDTH-50, SCREEN_HEIGHT/2};

    scoreLeft = 0;
    scoreRight = 0;
}

//###################################################

void randomiseBallSpeed() {
    if(coinFlip()) {
        ballSpeed.x = sign(ballSpeed.x) * 1;
        ballSpeed.y = sign(ballSpeed.y) * 2;
    } else {
        ballSpeed.x = sign(ballSpeed.x) * 2;
        ballSpeed.y = sign(ballSpeed.y) * 1;
    }
}

bool moveBall() {
    addPoints_ip(&ballPos, ballSpeed);

    if(ballPos.x <= 0) {
        ++scoreRight;
        return false;
    }
    if(ballPos.x >= SCREEN_WIDTH-BALL_DIM){
        ++scoreLeft;
        return false;
    }

    //bounce off screen top/bottom
    if(ballPos.y <= 0) {
        ballPos.y = 0;
        randomiseBallSpeed();
        ballSpeed.y = -ballSpeed.y;
    }
    if(ballPos.y >= SCREEN_HEIGHT-BALL_DIM) {
        ballPos.y = SCREEN_HEIGHT-BALL_DIM;
        randomiseBallSpeed();
        ballSpeed.y = -ballSpeed.y;
    }

    // bounce off paddles
    if(ballPos.x < paddleLeftPos.x+PADDLE_WIDTH && ballPos.x+BALL_DIM > paddleLeftPos.x &&
        ballPos.y < paddleLeftPos.y+PADDLE_HEIGHT && ballPos.y+BALL_DIM > paddleLeftPos.y
    ) {
        randomiseBallSpeed();
        ballSpeed.x = abs(ballSpeed.x);
    }

    if(ballPos.x < paddleRightPos.x+PADDLE_WIDTH && ballPos.x+BALL_DIM > paddleRightPos.x &&
        ballPos.y < paddleRightPos.y+PADDLE_HEIGHT && ballPos.y+BALL_DIM > paddleRightPos.y
    ) {
        randomiseBallSpeed();
        ballSpeed.x = -abs(ballSpeed.x);
    }
    return true;
}

//###################################################

#define PADDLE_SPEED 2
// paddles may not get closer than this to the screen borders
#define PADDLE_BORDER 5

void processKeyInput() {
    while(keyInputAvailable()) {
        switch(readASCII_blocking()) {
            case 'w':
                if(paddleLeftPos.y > PADDLE_BORDER)
                    paddleLeftPos.y -= PADDLE_SPEED;
                break;
            case 's':
                if(paddleLeftPos.y < SCREEN_HEIGHT-PADDLE_HEIGHT-PADDLE_BORDER)
                    paddleLeftPos.y += PADDLE_SPEED;
                break;
            /*
            case 'o':
                if(paddleRightPos.y > 20)
                    paddleRightPos.y -= 1;
                break;
            case 'l':
                if(paddleRightPos.y < SCREEN_HEIGHT-20)
                    paddleRightPos.y += 1;
                break;
            */
        }
    }
}

void moveAI() {
    short dx = paddleRightPos.x - ballPos.x;
    short y = ballPos.y + ballSpeed.y * dx / ballSpeed.x;
    if(y < paddleRightPos.y + PADDLE_HEIGHT/2 && paddleRightPos.y > PADDLE_BORDER)
        paddleRightPos.y -= PADDLE_SPEED;
    if(y > paddleRightPos.y + PADDLE_HEIGHT/2 && paddleRightPos.y < SCREEN_HEIGHT-PADDLE_HEIGHT-PADDLE_BORDER)
        paddleRightPos.y += PADDLE_SPEED;
}

//###################################################

void drawScores() {
    static const ushort scoreWidth = 1;
    static const ushort scoreHeight = 10;
    static const ushort scoreSpacing = 2;

    for(byte s = 0; s < scoreLeft; ++s) {
        fillArea(scoreSpacing + s * (scoreSpacing + scoreWidth), 5, scoreWidth, scoreHeight, 15);
    }

    for(byte s = 0; s < scoreRight; ++s) {
        fillArea(SCREEN_WIDTH - (s+1) * (scoreSpacing + scoreWidth), 5, scoreWidth, scoreHeight, 15);
    }
}

//###################################################
//###################################################

char *str = "x$\0";
void testKeyboard() {
    while(1) {
        *str = readASCII_blocking();
        print(str);
    }
}

int dosmain(void) {

    modeVGA256();

    while(1) {
        newGame();

        while(scoreLeft < MAX_SCORE && scoreRight < MAX_SCORE) {
            while(moveBall()) {
                processKeyInput();
                moveAI();

                clearScreen(0);

                fillArea(paddleLeftPos.x, paddleLeftPos.y, PADDLE_WIDTH, PADDLE_HEIGHT, 14);
                fillArea(paddleRightPos.x, paddleRightPos.y, PADDLE_WIDTH, PADDLE_HEIGHT, 14);
                fillArea(ballPos.x, ballPos.y, BALL_DIM, BALL_DIM, 15 );
                drawScores();

                vsync();
                flipBuffers();

                waitMillis(25);
            }
            newRound();
        }
    }

    return 0;
}
