//
// Simple test for the base chunk_alloc/free functions
//

use std::env;
use std::fs::File;
use std::io::{BufRead,BufReader,Error};
use std::path::{Path,PathBuf};
use std::str::Chars;

use clap::{App, Arg,ArgMatches};
use log::{debug,error}; // ,warn,error};

use leghorn::mmap::{Atom,ATOM_SHIFT,Mmap,MmapBuilder};

pub struct TestData<'a> {
    pub name: &'static str,
    pub args: ArgMatches<'a>,
    pub file_name: &'a Path,
}

pub trait TestJig {
    fn dump(&mut self, data: &mut TestData);
    fn alloc(&mut self, data: &mut TestData, slot: usize, size: usize);
    fn free(&mut self, data: &mut TestData, slot: usize, size: usize);
    fn finish(&mut self, data: &mut TestData);
}

pub fn run_test (data: &mut TestData, jig: &mut dyn TestJig)
                 -> Result<(), Error> {
    let file = File::open(&data.file_name)?;

    // uses a reader buffer
    let mut reader = BufReader::new(file);
    let mut line = String::new();

    loop {
        let bytes = reader.read_line(&mut line)?;
        if bytes == 0 {
            break;
        }
        trim_newline(&mut line);


        test_command(data, jig, &line);

        // do not accumulate data
        line.clear();
    }

    jig.finish(data);

    Ok(())
}

fn trim_newline(s: &mut String) {
    while s.ends_with('\n') || s.ends_with('\r') {
        s.pop();
    }
}

fn test_command (data: &mut TestData, jig: &mut dyn TestJig, s: &str) {
    debug!("command: {}", s);
    let mut c = s.chars();

    match c.next() {
        Some('d') => jig.dump(data),
        Some('a') => {
            debug!("alloc -----");
            let slot = make_number(&mut c);
            let size = make_number(&mut c);
            jig.alloc(data, slot, size);
        },
        Some('f') => {
            debug!("free -----");
            let slot = make_number(&mut c);
            let size = make_number(&mut c);
            jig.free(data, slot, size);
        }
        Some('#') => println!("comment"),
        _ => println!("unknown"),
    }
}

fn make_number(c: &mut Chars) -> usize {
    let mut val: usize = 0;
    for n in c {
        match n.to_digit(10) {
            Some(v) => { val = val * 10 + v as usize },
            None => break,
        }
    }

    val
}

pub fn logger_init(verbose: bool) {
    env::set_var("RUST_LOG", if verbose { "debug" } else { "warn" });

    let _ = env_logger::builder()
        .is_test(true)
        .format_timestamp(None)
        .try_init();
}

struct Data {
    data: [u8; 16],
}


#[allow(dead_code)]
struct Test01<'a> {
    max: usize,
    atoms: Vec<Atom>,
    sizes: Vec<usize>,
    mmap: &'a mut Mmap<'a>,
}

impl TestJig for Test01<'_> {
    fn dump(&mut self, _data: &mut TestData) {
        println!("test01 is dumping");
        self.mmap.dump();
    }

    fn alloc(&mut self, _data: &mut TestData, slot: usize, size: usize) {
        if self.sizes[slot] != 0 {
            panic!("alloc: size check fails: {} {}", self.sizes[slot], size);
        }

        if let Some(atom) = self.mmap.chunk_alloc(size) {
            self.atoms[slot] = atom;

            let datap: &mut Data
                = unsafe { self.mmap.atom_to_addr(atom.atom(), ATOM_SHIFT) };

            debug!("chunk_alloc for {:} got {:?} {:p}", size, atom, datap);
            for i in &mut datap.data[0..16] {
                *i = 0x45;
            }

        } else {
            error!("chunk_alloc for {} failed", size);
        }

        self.sizes[slot] = size;
    }

    fn free(&mut self, _data: &mut TestData, slot: usize, size: usize) {
        if self.sizes[slot] != size {
            panic!("free: size check fails: {} {}", self.sizes[slot], size);
        }

        let atom = self.atoms[slot];
        if !atom.is_null() {
            let datap: &mut Data
                = unsafe { self.mmap.atom_to_addr(atom.atom(), ATOM_SHIFT) };

            debug!("chunk_free for {:} got {:?} {:p}", size, atom, datap);
            for i in &datap.data[0..16] {
                assert_eq!(*i, 0x45);
            }
            for i in &mut datap.data[0..16] {
                *i = 0x65;
            }

            self.mmap.chunk_free(atom, self.sizes[slot]);
        }

        self.atoms[slot] = Atom::make_null();
        self.sizes[slot] = 0;
    }

    fn finish(&mut self, data: &mut TestData) {
        for i in 1 .. self.max {
            if self.sizes[i] != 0 {
                self.free(data, i, self.sizes[i]);
            }
        }
    }
}


fn main() {
    let args = App::new("pa01")
        .version("0.1.0")
        .author("phil@")
        .about("test program")
        .arg(
            Arg::with_name("base")
                .short("b")
                .long("base")
                .takes_value(true)
                .help("base directory name"),
        )
        .arg(
            Arg::with_name("data")
                .short("d")
                .long("data")
                .takes_value(true)
                .help("database file name"),
        )
        .arg(
            Arg::with_name("file")
                .help("data file name"),
        )
        .arg(
            Arg::with_name("max")
                .short("m")
                .long("max")
                .takes_value(true)
                .help("max number of slots"),
        )
        .arg(
            Arg::with_name("size")
                .short("s")
                .long("size")
                .takes_value(true)
                .help("database size"),
        )
        .arg(
            Arg::with_name("verbose")
                .short("v")
                .long("verbose")
                .help("generate verbose output"),
        )
        .get_matches();

    let base = args.value_of("base");
    println!("base is {}", base.unwrap_or("."));

    let max: usize = args.value_of("max")
        .unwrap_or("10000")
        .parse()
        .expect("invalid max argument");
    println!("max is {}", max);

    let mut atoms = Vec::<Atom>::with_capacity(max);
    atoms.resize(max, Atom::make_null());

    let mut sizes = Vec::<usize>::with_capacity(max);
    sizes.resize(max, 0);

    let f = args.value_of("file").unwrap().to_string();
    let mut p = PathBuf::new();
    if let Some(b) = base {
        p.push(b);
    }
    p.push(&f);
    let file_name = p.as_path();

    let size: usize = args.value_of("size")
        .unwrap_or("102400")
        .parse()
        .expect("invalid size argument");

    logger_init(true);

    let mut mmap = MmapBuilder::new()
        .path(PathBuf::from("test.mem"))
        .write(true)
        .mint(true)
        .size(size)
        .open().unwrap();

    debug!("got {:?}", mmap);
    mmap.dump();

    let mut test = Test01 { max, atoms, sizes, mmap: &mut mmap };
    let mut data = TestData { name: "test01", args, file_name };

    run_test(&mut data, &mut test).unwrap();

    test.mmap.chunk_check(true);
    test.mmap.close();
}
