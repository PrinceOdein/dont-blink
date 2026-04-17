#include <raylib.h>

int main()
{
	const int screenWidth = 800;
	const int screenHeight = 900;

	InitWindow(screenWidth, screenHeight, "Don't Blink");

	SetTargetFPS(24);

	while (!WindowShouldClose())
	{
		BeginDrawing();
		ClearBackground(DARKBLUE);
		EndDrawing();

	}
}