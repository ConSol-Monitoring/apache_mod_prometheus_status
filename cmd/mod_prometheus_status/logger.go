package main

import (
	"fmt"
	"os"
	"path/filepath"
	"runtime"

	"github.com/kdar/factorlog"
)

const (
	// LogFormat sets the log format
	LogFormat = `[%{Date} %{Time "15:04:05.000000"}][mod_prometheus_status][%{Severity}]%{Message}`

	// LogColors sets colors for some log levels
	LogColors = `%{Color "yellow" "WARN"}%{Color "red" "ERROR"}%{Color "red" "FATAL"}`

	// LogColorReset resets colors from LogColors
	LogColorReset = `%{Color "reset"}`

	// LogVerbosityNone disables logging
	LogVerbosityNone = 0

	// LogVerbosityDebug sets the debug log level
	LogVerbosityDebug = 2
)

// initialize standard logger which will be configured later from the configuration file options
var logger *factorlog.FactorLog

// initLogging initializes the logging system.
func initLogging() {
	logger = factorlog.New(os.Stderr, factorlog.NewStdFormatter(LogColors+LogFormat+LogColorReset))

	switch EnableDebug {
	case "1":
		logger.SetMinMaxSeverity(factorlog.StringToSeverity("DEBUG"), factorlog.StringToSeverity("PANIC"))
		logger.SetVerbosity(LogVerbosityDebug)
	default:
		logger.SetMinMaxSeverity(factorlog.StringToSeverity("PANIC"), factorlog.StringToSeverity("PANIC"))
		logger.SetVerbosity(LogVerbosityNone)
	}
}

func log(format string, v ...interface{}) {
	msg := fmt.Sprintf(format, v...)
	_, file, line, _ := runtime.Caller(1)
	logger.Debugf(fmt.Sprintf("[pid:%d][%s:%d] %s", os.Getpid(), filepath.Base(file), line, msg))
}
