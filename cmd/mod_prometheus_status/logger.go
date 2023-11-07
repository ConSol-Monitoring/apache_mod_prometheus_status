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
	LogFormat = `[%{Time "Mon Jan 02 15:04:05.000000 2006"}] [mod_prometheus_status:%{severity}] [pid %{Pid}:tid ???] %{Message}`

	// LogVerbosityDebug sets the debug log level
	LogVerbosityDebug = 2

	// LogVerbosityInfo sets the info log level
	LogVerbosityInfo = 3

	// LogVerbosityError sets the error log level
	LogVerbosityError = 4
)

// initialize standard logger which will be configured later from the configuration file options.
var logger *factorlog.FactorLog

// initLogging initializes the logging system.
func initLogging(enableDebug int) {
	if logger != nil {
		return
	}
	logger = factorlog.New(os.Stderr, factorlog.NewStdFormatter(LogFormat))

	logger.SetVerbosity(LogVerbosityDebug)
	switch enableDebug {
	case 1:
		logger.SetMinMaxSeverity(factorlog.StringToSeverity("DEBUG"), factorlog.StringToSeverity("PANIC"))
	default:
		logger.SetMinMaxSeverity(factorlog.StringToSeverity("INFO"), factorlog.StringToSeverity("PANIC"))
	}
}

func logf(lvl int, format string, v ...interface{}) {
	msg := fmt.Sprintf(format, v...)
	_, file, line, _ := runtime.Caller(2)
	logmsg := fmt.Sprintf("[%s:%d] %s", filepath.Base(file), line, msg)
	switch lvl {
	case LogVerbosityError:
		logger.Errorf(logmsg)
	case LogVerbosityInfo:
		logger.Infof(logmsg)
	case LogVerbosityDebug:
		logger.Debugf(logmsg)
	default:
	}
}

func logErrorf(format string, v ...interface{}) {
	logf(LogVerbosityError, format, v...)
}

func logInfof(format string, v ...interface{}) {
	logf(LogVerbosityInfo, format, v...)
}

func logDebugf(format string, v ...interface{}) {
	logf(LogVerbosityDebug, format, v...)
}
