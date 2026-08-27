"""Shared loading helpers for the local CLAP runtime."""

import threading


_MODEL_CACHE = {}
_MODEL_CACHE_LOCK = threading.Lock()


def install_model_cache(runtime):
    """Cache CLAP models inside a persistent worker process."""
    current = runtime.get_model
    if getattr(current, "_semantifx_model_cache", False):
        return current

    original = current

    def cached_get_model(model_choice):
        key = (original, model_choice)
        with _MODEL_CACHE_LOCK:
            if key in _MODEL_CACHE:
                cached_get_model.cache_hits += 1
                return _MODEL_CACHE[key]
            cached_get_model.cache_misses += 1
            model = original(model_choice)
            _MODEL_CACHE[key] = model
            return model

    cached_get_model._semantifx_model_cache = True
    cached_get_model.cache_hits = 0
    cached_get_model.cache_misses = 0
    runtime.get_model = cached_get_model
    return cached_get_model
