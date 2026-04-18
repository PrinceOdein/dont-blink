#include <raylib.h>

int main()
{
	const int screenWidth = 1900;
	const int screenHeight = 1000;

	InitWindow(screenWidth, screenHeight, "Don't Blink");

	SetTargetFPS(24);

	while (!WindowShouldClose())
	{
		BeginDrawing();
		ClearBackground(DARKBLUE);
		EndDrawing();

	}
}