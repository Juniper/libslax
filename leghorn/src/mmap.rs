// mmap(addr: *mut c_void, len: size_t, prot: c_int, flags: c_int, fd: c_int, offset: off_t) -> *mut c_void

extern crate libc;

// use std::mem;
use std::default::Default;
use std::fmt;
use std::fmt::Debug;
use std::fs;
use std::fs::File as FsFile;
use std::fs::OpenOptions;
use std::io;
use std::os::unix::fs::OpenOptionsExt;
use std::os::unix::io::AsRawFd;
use std::path::PathBuf;
// use std::ptr;
// use std::slice;

use libc::{c_int, c_void};

use snafu::{ResultExt, Snafu};

#[derive(Debug, Snafu)]
pub enum Error {
    #[snafu(display("Could not open input file '{}': {}",
                    path.display(), source))]
    BadFile {
        path: PathBuf,
        source: std::io::Error,
    },

    #[snafu(display("Could not map data: {}", source))]
    BadMmap { source: std::io::Error },

    #[snafu(display("Bad info header: {} (0x{:#x})", message, value))]
    BadInfo { message: &'static str, value: u32 },

    #[snafu(display("unknown error"))]
    Unknown,
}

// Default address at which we map
pub const ADDR_DEFAULT: usize = 0x2000_0000_0000;
pub const ADDR_DEFAULT_INCR: usize = 0x020000000000;

pub const ATOM_SHIFT: usize = 12;
pub const ATOM_SIZE: usize = 1 << ATOM_SHIFT;

pub const HEADER_NAME_LEN: usize = 64; // Max length of header name string

// Default initial mmap file size
pub const DEFAULT_COUNT: usize = 32;
pub const DEFAULT_SIZE: usize = DEFAULT_COUNT << ATOM_SHIFT;

pub type MmapAddress = c_void; // Base address type (aka 'u8')
pub type MmapAtom = u32; // Base atom type
pub type MmapAtomCount = u32; // Used for number of atoms

const VERS_MAJOR: u8 = 1; // Major numbers are incompatible
const VERS_MINOR: u8 = 0; // Minor number are compatible

const MAGIC_NUMBER: u16 = 0xBE1E; // "Our" endian-ness magic
const MAGIC_WRONG: u16 = 0x1EBE; // "Wrong" endian-ness magic

// const FREE_MAGIC: u32 = 0xCABB1E16;  // Denotes free atoms

/// This header appears at the front of a memory file, helping to
/// verify that we are looking at a file we have constructed.
#[repr(C)]
pub struct MmapInfo {
    magic: u16,       // Magic number
    vers_major: u8,   // Major version number
    vers_minor: u8,   // Minor verision number
    size: usize,      // Current size of the map file
    max_size: usize,  // Hard limit on the map file size (or 0)
    num_headers: u32, // Number of headers following our's
    reserved: u32,    // Reserved (padding)
}

#[repr(C)]
struct MmapFree {
    magic: u32,          // Magic number (FREE_MAGIC)
    size: MmapAtomCount, // Number of atoms free here
    next: MmapAtom,      // Next atom in this free list
}

#[repr(C)]
struct MmapHeader {
    name: [u8; HEADER_NAME_LEN], // Simple text name
    type_: u16,                  // Type of this data
    flags: u16,                  // Bitflags for this data (unused)
    size: u32,                   // Length of this header (in bytes)
                                 // content of header follows immediately
}

#[allow(dead_code)] // XXX under construction
struct MmapSegment {
    address: *mut MmapAddress, // Address at which this object is mapped
    size: usize,               // Length of mapped object
}

/// Mmap supports two styles: File, Anonymous.  Unset is used during
/// building, but has no real meaning.
#[allow(dead_code)]
enum MmapStyle {
    Unset, // Uninitialized
    File {
        path: PathBuf, // Name of file mapped
        file: FsFile,  // File being mapped
        fd: c_int,     // Fd of the file (lifetimed as 'file')
        write: bool,   // Mapped as writable
    },
    Anonymous {
        // Anonymous memory (aka MAP_ANON)
        segments: Vec<MmapSegment>, // Set of segments currently mapped
    },
}

#[allow(dead_code)] // XXX under construction
pub struct Mmap<'a> {
    address: *mut MmapAddress, // Address at which this object is mapped
    size: usize,               // Length of mapped object
    style: MmapStyle,          // Per-style information
    info: &'a mut MmapInfo,    // Our main header
}

#[derive(Debug, Default)]
pub struct MmapBuilder {
    path: Option<PathBuf>, // Name of file mapped
    write: bool,           // Mapped as writable
    private: bool,         // Private contents; not world-readable
    size: usize,           // Desired size of the file
}

impl MmapBuilder {
    pub fn new() -> MmapBuilder {
        MmapBuilder {
            path: None,
            write: true,
            private: true,
            size: 0,
        }
    }

    pub fn path(mut self, path: PathBuf) -> Self {
        self.path = Some(path);
        self
    }

    pub fn write(mut self, val: bool) -> Self {
        self.write = val;
        self
    }

    pub fn private(mut self, val: bool) -> Self {
        self.private = val;
        self
    }

    pub fn size(mut self, size: usize) -> Self {
        self.size = size;
        self
    }
}

impl<'a> MmapInfo {
    fn create(address: *mut MmapAddress, size: usize,
                  created: bool)
                  -> Result<&'a mut MmapInfo, Error> {

        let info: &mut MmapInfo =
            unsafe { std::mem::transmute::<_, _>(address) };

        // If we created the database, fill in the information; if
        // not, sanity check the values.
        if created {
            info.magic = MAGIC_NUMBER;
            info.vers_major = VERS_MAJOR;
            info.vers_minor = VERS_MINOR;
            info.size = size;
            info.max_size = 0;
            info.num_headers = 0;

        } else {
            if info.magic == MAGIC_WRONG {
                return Err(Error::BadInfo {
                    message: "magic has endian issues",
                    value: info.magic as u32,
                });
            } else if info.magic != MAGIC_NUMBER {
                return Err(Error::BadInfo {
                    message: "magic has wrong value",
                    value: info.magic as u32,
                });
            } else if info.vers_major != VERS_MAJOR {
                return Err(Error::BadInfo {
                    message: "version number is wrong",
                    value: info.vers_major as u32,
                });
            }
        }
    
        Ok(info)
    }
}

impl<'a> MmapBuilder {
    pub fn open(self) -> Result<Mmap<'a>, Error> {
        let mut address: *mut MmapAddress;
        let mut size = self.size;
        let style;
        let fd;
        let mut prot: c_int = libc::PROT_READ;
        let created;

        if let Some(path) = self.path {
            let write = self.write;

            // We can set the write flag knowning that it will only
            // matter if the file is created, which only happens if
            // 'write' is true.
            let perm = if self.private { 0o600 } else { 0o644 };

            created = fs::metadata(&path).is_ok();

            let file = OpenOptions::new()
                .read(true)
                .write(write)
                .create(write)
                .mode(perm)
                .open(&path)
                .context(BadFile { path: &path })?;

            if write {
                prot |= libc::PROT_WRITE;
            }
            fd = file.as_raw_fd();

            // If the file is larger, use it's size.
            // If the file is smaller, we need to get a bigger size.
            if let Ok(md) = file.metadata() {
                let cur = md.len() as usize;
                if size < cur {
                    size = cur; // Use current file size
                } else if let Err(e) = file.set_len(size as u64) {
                    return Err(Error::BadMmap { source: e });
                }
            }

            style = MmapStyle::File { path, file, fd, write };
        } else {
            // If there was no path defined, then we are Anonymous
            fd = -1;
            created = true;

            let segments = Vec::new();
            style = MmapStyle::Anonymous { segments };
        }

        // XXX This needs to be a mutex-protected global that is moved
        // when we make a map so that we have a sufficient gap between
        // the top of our fresh map and the start of the next one.
        // But for now, this works.....
        address = ADDR_DEFAULT as _;

        let flags: c_int = libc::MAP_SHARED | libc::MAP_FIXED;

        let mut addr2 = libc::MAP_FAILED;

        unsafe {
            for _ in 0..10 {
                println!(
                    "libc::mmap: a: {:#x}, s: {}, p: {}, f: {}, fd:, {}",
                    address as usize, size, prot, flags, fd
                );
                addr2 = libc::mmap(address, size as _, prot, flags, fd, 0);
                println!("got {:#x}", addr2 as usize);
                if addr2 == address {
                    break;
                }

                let mut off: usize = address as _;
                off += ADDR_DEFAULT_INCR;
                address = off as _;
            }
        }

        if addr2 == libc::MAP_FAILED {
            return Err(Error::BadMmap {
                source: io::Error::last_os_error(),
            });
        }

        let &mut info = MmapInfo::create(address, size, created)?;

        Ok(Mmap { address, size, style, info: &mut info })
    }
}

impl Debug for MmapStyle {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            MmapStyle::Unset => f.debug_struct("MmapStyle::Unset").finish(),
            MmapStyle::File { path, file, fd, write } => {
                f.debug_struct("MmapStyle::File")
                    .field("path", &path)
                    .field("file", &file)
                    .field("fd", &fd)
                    .field("write", &write)
                    .finish()
            },
            MmapStyle::Anonymous { segments } => {
                f.debug_struct("MmapStyle::Anonymous")
                    .field("num-segments", &segments.len())
                    .finish()
            },
        }
    }
}

impl Debug for MmapInfo {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("MmapInfo")
            .field("magic", &format_args!("0x{:#x}", &self.magic))
            .field("vers_major", &self.vers_major)
            .field("vers_minor", &self.vers_minor)
            .field("size", &self.size)
            .field("max_size", &self.max_size)
            .field("num_headers", &self.num_headers)
            .finish()
    }
}

impl Debug for Mmap<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let off = self.address as usize;
        f.debug_struct("Mmap")
            .field("address", &format_args!("0x{:#x}", off))
            .field("size", &self.size)
            .field("style", &self.style)
            .field("style", &self.info)
            .finish()
    }
}

#[test]
fn simple_open() {
    let m = MmapBuilder::new()
        .path(PathBuf::from("test.mem"))
        .write(true)
        .size(100 * 1024)
        .open();

    println!("got {:?}", m);
}
