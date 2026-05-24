package cz.eidam.lib

/** Convenience factory for engines on Android. */
object Engines {
    fun aubio(options: EngineOptions = EngineOptions()): AudioEngine = AubioEngine(options)
}
