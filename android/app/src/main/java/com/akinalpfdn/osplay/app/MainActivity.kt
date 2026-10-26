package com.akinalpfdn.osplay.app

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import com.akinalpfdn.osplay.ui.OsplayApp
import com.akinalpfdn.osplay.ui.theme.OsplayTheme

/** The only activity. It handles rotation itself (the manifest's configChanges), so the session survives it. */
class MainActivity : ComponentActivity() {
    private val viewModel: MainViewModel by viewModels {
        object : ViewModelProvider.Factory {
            override fun <T : ViewModel> create(modelClass: Class<T>): T {
                @Suppress("UNCHECKED_CAST") // the only view model this factory makes
                return MainViewModel((application as OsplayApplication).dependencies) as T
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        val stage = (application as OsplayApplication).dependencies.stage
        setContent {
            OsplayTheme {
                OsplayApp(viewModel, stage)
            }
        }
    }

    override fun onStart() {
        super.onStart()
        viewModel.onForeground()
    }

    override fun onStop() {
        viewModel.onBackground()
        super.onStop()
    }
}
