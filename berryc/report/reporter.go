package report

import "sync"

type Reporter interface {
	ReportError(err error)
}

/* -------------------------------------------------------------------------- */

var globalRep Reporter
var repM *sync.Mutex
var errCount int

func SetGlobalReporter(rep Reporter) {
	globalRep = rep
	repM = &sync.Mutex{}
}

func Error(err error) {
	defer repM.Unlock()
	repM.Lock()

	globalRep.ReportError(err)

	errCount += 1
}

func NoErrors() bool {
	return errCount == 0
}
