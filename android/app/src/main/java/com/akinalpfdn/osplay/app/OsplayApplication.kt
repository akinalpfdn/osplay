package com.akinalpfdn.osplay.app

import android.app.Application

/** Holds the composition root for the activity's view model. */
class OsplayApplication : Application() {
    val dependencies: AppDependencies by lazy { AppDependencies.create(this) }
}
