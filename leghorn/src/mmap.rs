// mmap(addr: *mut c_void, len: size_t, prot: c_int, flags: c_int, fd: c_int, offset: off_t) -> *mut c_void

extern crate libc;

// use std::mem;
use std::fs::File;
use std::path::PathBuf;
use std::default::Default;
use std::fmt;
use std::fmt::Debug;
// use std::ptr;

use libc::{c_int}; // ,c_void};

use snafu::{ResultExt, Snafu};

#[derive(Debug, Snafu)]
pub enum Error {
    #[snafu(display("Could not open input file '{}': {}",
                    path.display(), source))]
    FileError {
        path: PathBuf,
        source: std::io::Error,
    },
}

pub type MmapAddress = libc::c_void;  // Base address type

#[allow(dead_code)]             // XXX under construction
struct MmapObject {
    address: *mut MmapAddress,  // Address at which this object is mapped
    size: usize,                // Length of mapped object
}

#[allow(dead_code)]             // XXX under construction
pub struct Mmap {
    path: Option<PathBuf>,   // Name of file mapped
    file: Option<File>,      // Rust file being mapped
    read_only: bool,         // Mapped as read-only
    prot: c_int,             // File protection (c_int, libc::PROT_*)
    address: *mut MmapAddress,   // Address at which this object is mapped
    size: usize,           // Length of mapped object
    segments: Vec<MmapObject>, // Set of segments currently mapped
}

#[derive(Debug,Default)]
pub struct MmapBuilder {
    path: Option<PathBuf>,     // File to be mapped
    read_only: bool,           // Should the file be opened read-only?
    size: usize,               // Desired size of the file
}

impl MmapBuilder {
    pub fn new () -> MmapBuilder {
        MmapBuilder { path: None, read_only: false, size: 0 }
    }

    pub fn path (mut self, path: PathBuf) -> MmapBuilder {
        self.path = Some(path);
        self
    }

    pub fn read_only (mut self) -> MmapBuilder {
        self.read_only = true;
        self
    }

    pub fn size (mut self, size: usize) -> MmapBuilder {
        self.size = size;
        self
    }

    pub fn open (self) -> Result<Mmap,Error> {
        let path = self.path;
        let mut file: Option<File> = None;
        let read_only = self.read_only;
        let prot: c_int = 0;
        let address: *mut MmapAddress = libc::MAP_FAILED;
        let size = self.size;

        if let Some(p) = &path {
            let f = File::open(p).context(FileError { path: p })?;
            file = Some(f);
        } else {
            // ...
        }

        Ok(Mmap { path, file, read_only, prot, address, size,
                  segments: Vec::new() })
    }
}

impl Debug for Mmap {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("Mmap")
         .field("path", &self.path)
         .field("file", &self.file)
         .field("read_only", &self.read_only)
         .field("size", &self.size)
         .finish()
    }    
}

#[test]
fn simple_open () {
    let m = MmapBuilder::new().path(PathBuf::from("test.mem")).size(100 * 1024).open();

    println!("got {:?}", m);
}
