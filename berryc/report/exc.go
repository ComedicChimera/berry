package report

var throwMagic int = 0xdeadbeef

func Throw(err error) {
	Error(err)
	panic(throwMagic)
}

func Catch() {
	if x := recover(); x != nil {
		if xMagic, ok := x.(int); ok && xMagic == throwMagic {
			// Nothing to do: we all good
		} else {
			panic(x)
		}
	}
}
