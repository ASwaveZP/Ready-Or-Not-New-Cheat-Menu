#pragma once

// Keeps mouse ownership in sync between the WPF menu and the game.
//
// The menu draws its own cursor (CursorOverlay) and takes the mouse away from
// the game completely while it is open. The game still has to release the
// viewport mouse capture, otherwise it recenters the OS cursor every frame and
// camera look fights the menu.
//
//   menu visible -> capture released, both game cursor flags pinned to false,
//                  and every mouse message to the game window dropped
//   menu hidden  -> game input mode restored, capture back, camera look locked
//
// Requests may be queued from any thread; they are applied on the game thread
// because both the controller fields and ChangeInputMode() touch Slate/input
// state.
namespace InputOwnership {
    // Queues a transition to "menu open" / "menu closed".
    void RequestMenuState(bool menuOpen);

    // Applies a queued transition. Must be called from the game thread.
    void ApplyPending();

    // One-shot, self-guarding: moves the game off exclusive fullscreen the first
    // time the user settings object is available. Safe to call every frame.
    // Exclusive fullscreen cannot coexist with a topmost overlay window, so this
    // is what stops the menu from kicking the game out of fullscreen.
    void EnsureDisplayMode();

    // True while the menu owns the mouse. The game window procedure uses this to
    // drop every mouse message before the game can read the pointer position.
    bool IsMenuOpen();
}
