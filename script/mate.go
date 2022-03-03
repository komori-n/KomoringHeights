package main

import (
	"bufio"
	"fmt"
	"io"
	"os"
	"os/exec"
	"strings"
	"time"

	"github.com/schollz/progressbar"

	flag "github.com/spf13/pflag"
)

type Options struct {
	HashSize        int
	PostSearchCount int
	DepthLimit      int
	TimeLimit       int
	OutFile         string
	Process         int
}

func parseOptions() Options {
	hash_size := flag.IntP("hash", "h", 64, "the size of hash (MB)")
	post_search_count := flag.IntP("post-search-count", "c", 0, "the number of post-search moves")
	depth_limit := flag.IntP("mate-limit", "m", 0, "the maximum mate length")
	time_limit := flag.IntP("time-limit", "t", 0, "the maximum time (msec)")
	out_file := flag.StringP("out", "o", "", "the output file")
	num_process := flag.IntP("process", "p", 4, "the number of process")
	flag.Parse()

	return Options{
		HashSize:        *hash_size,
		PostSearchCount: *post_search_count,
		DepthLimit:      *depth_limit,
		TimeLimit:       *time_limit,
		OutFile:         *out_file,
		Process:         *num_process,
	}
}

type EngineProcess struct {
	cmd     *exec.Cmd
	stdin   io.WriteCloser
	stdout  io.ReadCloser
	scanner *bufio.Scanner
}

func newEngineProcess(command string) (*EngineProcess, error) {
	cmd := exec.Command(command)
	stdin, err := cmd.StdinPipe()
	if err != nil {
		return nil, err
	}

	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return nil, err
	}
	scanner := bufio.NewScanner(stdout)

	err = cmd.Start()
	if err != nil {
		return nil, err
	}

	return &EngineProcess{cmd, stdin, stdout, scanner}, nil
}

func (ep *EngineProcess) SetOption(op Options) {
	fmt.Fprintf(ep.stdin, "setoption name USI_Hash value %d\n", op.HashSize)
	fmt.Fprintf(ep.stdin, "setoption name PostSearchCount value %d\n", op.PostSearchCount)
	fmt.Fprintf(ep.stdin, "setoption name DepthLimit value %d\n", op.DepthLimit)
	fmt.Fprintf(ep.stdin, "setoption name RootIsAndNodeIfChecked value false\n")
	fmt.Fprintf(ep.stdin, "setoption name PvInterval value 0\n")
	fmt.Fprintf(ep.stdin, "setoption name YozumePrintLevel value 0\n")
}

func (ep *EngineProcess) Ready() error {
	fmt.Fprintln(ep.stdin, "isready")

	for ep.scanner.Scan() {
		text := ep.scanner.Text()
		if strings.Contains(text, "readyok") {
			return nil
		}
	}

	err := ep.scanner.Err()
	if err != nil {
		return err
	}

	return fmt.Errorf("got no \"readyok\"")
}

func (ep *EngineProcess) solveImpl(sfen string) error {
	fmt.Fprintf(ep.stdin, "sfen %s\n", sfen)
	fmt.Fprintln(ep.stdin, "go mate infinite")

	for ep.scanner.Scan() {
		text := ep.scanner.Text()
		switch {
		case strings.Contains(text, "nomate"):
			return fmt.Errorf("got nomate")
		case strings.Contains(text, "Failed to detect PV"):
			return fmt.Errorf("Failed to detect PV")
		case strings.Contains(text, "checkmate "):
			if text == "checkmate " {
				return fmt.Errorf("got checkout without mate moves")
			} else if text == "checkmate timeout" {
				return fmt.Errorf("timeout")
			} else {
				return nil
			}
		}
	}
	err := ep.scanner.Err()
	if err != nil {
		return err
	}

	return fmt.Errorf("unexpected EOF")
}

func (ep *EngineProcess) Solve(sfen string, time_limit_ms int) error {
	if time_limit_ms == 0 {
		return ep.solveImpl(sfen)
	}

	timer := time.NewTimer(time.Duration(time_limit_ms) * time.Millisecond)
	result := make(chan error)
	go func() {
		err := ep.solveImpl(sfen)
		result <- err
	}()

	select {
	case <-timer.C:
		fmt.Fprintln(ep.stdin, "stop")
		<-result
		return fmt.Errorf("time limit exceeded")
	case res := <-result:
		if !timer.Stop() {
			<-timer.C
		}
		return res
	}
}

type Summary struct {
	total  int
	solved int
}

func solve(
	bar *progressbar.ProgressBar,
	command string, op Options,
	sfen_input chan string,
	output_ch chan string,
	summary_ch chan Summary) {
	process, err := newEngineProcess(command)
	if err != nil {
		fmt.Println("error:", err)
		os.Exit(1)
	}
	process.SetOption(op)
	err = process.Ready()
	if err != nil {
		fmt.Println("error:", err)
		os.Exit(1)
	}

	total := 0
	solved := 0
	for sfen := range sfen_input {
		total += 1
		err := process.Solve(sfen, op.TimeLimit)
		if err != nil {
			output_ch <- fmt.Sprintf("%v: sfen %v", err, sfen)
		} else {
			solved += 1
		}
		bar.Add(1)
	}

	summary_ch <- Summary{total, solved}
}

func main() {
	op := parseOptions()

	if flag.NArg() == 0 {
		fmt.Println("error: solver command was not specified")
		flag.Usage()
		os.Exit(1)
	}

	start := time.Now()
	bar := progressbar.Default(-1)

	command := flag.Arg(0)
	sfen_chan := make(chan string)
	output_chan := make(chan string)
	summary_chan := make(chan Summary)
	for i := 0; i < op.Process; i++ {
		go solve(bar, command, op, sfen_chan, output_chan, summary_chan)
	}

	end := make(chan struct{}, 1)
	go func() {
		defer close(end)

		has_outfile := false
		var outfile *os.File
		defer outfile.Close()
		if op.OutFile != "" {
			file, err := os.Create(op.OutFile)
			if err == nil {
				has_outfile = true
				outfile = file
			}
		}

		total := 0
		solved := 0
		running := op.Process
		for {
			select {
			case out := <-output_chan:
				fmt.Printf("\r%v\n", out)
				if has_outfile {
					fmt.Fprintf(outfile, "\r%v\n", out)
				}
			case summary := <-summary_chan:
				total += summary.total
				solved += summary.solved
				running -= 1
				if running <= 0 {
					fmt.Println()
					fmt.Printf("solved/total: %v/%v  (%.2f sec)\n", solved, total, time.Since(start).Seconds())
					if has_outfile {
						fmt.Fprintf(outfile, "solved/total: %v/%v   (%.2f sec)\n", solved, total, time.Since(start).Seconds())
					}
					return
				}
			}
		}
	}()

	sfen_scanner := bufio.NewScanner(os.Stdin)
	for sfen_scanner.Scan() {
		sfen := sfen_scanner.Text()
		sfen_chan <- sfen
	}
	close(sfen_chan)

	<-end
}
