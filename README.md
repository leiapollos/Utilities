# Utilities

Personal utilities library for my other projects. Currently contains a custom arena memory allocator.

## What is this?

This is my personal collection of utilities that I use across different projects.

## Current Utilities

### Arena Allocator

```cpp
// Allocate arena
Arena* arena = arena_alloc();

// Get memory from arena
void* data = arena_push(arena, MB(1));

// Use memory...

// Release everything at once
arena_release(arena);
```

## Future Utilities

I'll add more utilities here as I need them for other projects.

## Inspiration

This project is heavily inspired by [Handmade Hero](https://handmadehero.org/) by Casey Muratori.

---

*Personal utilities by André Leite* 