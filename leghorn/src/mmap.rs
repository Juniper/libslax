// mmap(addr: *mut c_void, len: size_t, prot: c_int, flags: c_int, fd: c_int, offset: off_t) -> *mut c_void

extern crate libc;

// use std::mem;
// use std::ptr::{Unique, self};
// use std::ptr::{NonNull};
// use std::ops::{Deref, DerefMut};
// use std::cmp::{Ordering,PartialOrd};
use std::default::Default;
use std::fmt;
use std::fmt::Debug;
use std::fs;
use std::fs::File as FsFile;
use std::fs::OpenOptions;
use std::io;
use std::mem;
// use std::ops::{Add,Sub};
use std::ops::{AddAssign,SubAssign};
use std::os::unix::fs::OpenOptionsExt;
use std::os::unix::io::AsRawFd;
use std::path::PathBuf;
// use std::ptr;
// use std::slice;

use log::{debug,warn};
use libc::c_int;
use snafu::{ResultExt, Snafu};

#[cfg(test)]
use log::error;

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

type OffsetType = u32; // We only need 2^32 of addresses

// Default address at which we map
const ADDR_DEFAULT: usize = 0x2000_0000_0000;
const ADDR_DEFAULT_INCR: usize = 0x020000000000;
const ADDR_ALIGN: OffsetType = mem::size_of::<u64>() as OffsetType;

#[allow(dead_code)]
pub(crate) const ATOM_SHIFT: u32 = 12;
#[allow(dead_code)]
pub(crate) const ATOM_SIZE: usize = 1 << ATOM_SHIFT;

/// The basic "null" atom number
pub(crate) const ATOM_NULL: RawAtom = 0;

#[allow(dead_code)]
pub(crate) const HEADER_NAME_LEN: usize = 64; // Max length of header name string

// Default initial mmap file size
pub(crate) const DEFAULT_COUNT: RawAtom = 32;
pub(crate) const DEFAULT_SIZE: usize = (DEFAULT_COUNT as usize) << ATOM_SHIFT;

type RawAtom = u32; // Base 'atom' type

#[derive(Clone,Copy,Debug)]
pub(crate) struct Address(usize); // Base address type

#[derive(Clone,Copy,Debug)]
pub struct Atom(RawAtom); // Base atom type

#[derive(Clone,Copy,Debug,PartialEq,PartialOrd)]
pub(crate) struct AtomCount(u32); // Used for number of atoms

#[derive(Clone,Copy,Debug)]
pub(crate) struct FreeAtom(RawAtom); // Atom on the free list

const VERS_MAJOR: u8 = 1; // Major numbers are incompatible
const VERS_MINOR: u8 = 0; // Minor number are compatible

const MAGIC_NUMBER: u16 = 0xBE1E; // "Our" endian-ness magic
const MAGIC_WRONG: u16 = 0x1EBE; // "Wrong" endian-ness magic

const FREE_MAGIC: u32 = 0xCABB1E16;  // Denotes free atoms

/// This header appears at the front of a memory file, helping to
/// verify that we are looking at a file we have constructed.
#[repr(C)]
pub(crate) struct MmapInfo {
    magic: u16,       // Magic number
    vers_major: u8,   // Major version number
    vers_minor: u8,   // Minor verision number
    size: usize,      // Current size of the map file
    max_size: usize,  // Hard limit on the map file size (or 0)
    num_headers: u32, // Number of headers following our's
    free: FreeAtom,   // Reserved (padding)
}

const MMAP_INFO_SIZE: OffsetType = mem::size_of::<MmapInfo>() as OffsetType;

#[repr(C)]
#[allow(dead_code)]
struct MmapFree {
    magic: u32,          // Magic number (FREE_MAGIC)
    size: AtomCount,     // Number of atoms free here
    next: FreeAtom,      // Next atom in this free list (0 == None)
}

#[repr(C)]
#[allow(dead_code)]
struct MmapHeader {
    name: [u8; HEADER_NAME_LEN], // Simple text name
    kind: u16,                   // Type of this data
    flags: u16,                  // Bitflags for this data (unused)
    size: u32,                   // Length of this header (in bytes)
                                 // Content of header follows immediately
}

#[allow(dead_code)] // XXX under construction
struct MmapSegment {
    address: Address,          // Address at which this object is mapped
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

/// Offset of an internal header (MmapHeader) inside the memory segment
#[derive(Copy,Clone,Debug)]
struct Offset(OffsetType);

#[allow(dead_code)] // XXX under construction
pub struct Mmap<'a> {
    address: Address,          // Address at which this object is mapped
    size: usize,               // Length of mapped object
    style: MmapStyle,          // Per-style information
    info: &'a mut MmapInfo,    // Our main 'info' header
    headers: Vec<Offset>,      // Offsets of each of the headers
}

#[derive(Debug, Default)]
pub struct MmapBuilder {
    path: Option<PathBuf>, // Name of file mapped
    write: bool,           // Mapped as writable
    private: bool,         // Private contents; not world-readable
    size: usize,           // Desired size of the file
}


/// Simpliest "round up" function, suitable for any holiday party
fn round_up_u32(val: u32, to: u32) -> u32 {
    (val + to - 1) & !(to - 1)
}

/// Simpliest "round up" function, suitable for any holiday party
#[allow(dead_code)]
fn round_up_u64(val: u64, to: u64) -> u64 {
    (val + to - 1) & !(to - 1)
}

/// Simpliest "round up" function, suitable for any holiday party
#[allow(dead_code)]
fn round_up_usize(val: usize, to: usize) -> usize {
    (val + to - 1) & !(to - 1)
}

/// Simpliest "round up" function, suitable for any holiday party
#[allow(dead_code)]
fn round_up_atom(val: RawAtom, to: RawAtom) -> RawAtom {
    (val + to - 1) & !(to - 1)
}

/// Simple function to align byte counts to alignment
fn align_offset(value: OffsetType) -> OffsetType {
    round_up_u32(value, ADDR_ALIGN)
}


impl MmapBuilder {
    pub fn new() -> MmapBuilder {
        MmapBuilder {
            path: None,
            write: true,
            private: true,
            size: DEFAULT_SIZE,
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
        self.size = round_up_usize(size, ATOM_SIZE);
        self
    }
}

impl MmapInfo {
    fn validate(&mut self, addr: usize, size: usize, created: bool)
              -> Result<(), Error> {

        // If we created the database, fill in the information; if
        // not, sanity check the values.
        if created {
            self.magic = MAGIC_NUMBER;
            self.vers_major = VERS_MAJOR;
            self.vers_minor = VERS_MINOR;
            self.size = size;
            self.max_size = 0;
            self.num_headers = 0;

            let off: usize = ATOM_SIZE;
            let fp: &mut MmapFree
                = unsafe { mem::transmute::<_,_>(addr + off) };

            fp.magic = FREE_MAGIC;
            fp.size = AtomCount((size >> ATOM_SHIFT) as RawAtom - 1);
            fp.next = FreeAtom(0);

            self.free = FreeAtom(1);

        } else {
            if self.magic == MAGIC_WRONG {
                return Err(Error::BadInfo {
                    message: "magic has endian issues",
                    value: self.magic as u32,
                });
            } else if self.magic != MAGIC_NUMBER {
                return Err(Error::BadInfo {
                    message: "magic has wrong value",
                    value: self.magic as u32,
                });
            } else if self.vers_major != VERS_MAJOR {
                return Err(Error::BadInfo {
                    message: "version number is wrong",
                    value: self.vers_major as u32,
                });
            } else if self.vers_minor > VERS_MINOR {
                return Err(Error::BadInfo {
                    message: "minor version number is too new",
                    value: self.vers_minor as u32,
                });
            }
        }
    
        Ok(())
    }
}

impl MmapBuilder {
    fn do_map (fd: c_int, size: usize, prot: c_int)
               -> Result<usize,Error> {

        // XXX This needs to be a mutex-protected global that is moved
        // when we make a map so that we have a sufficient gap between
        // the top of our fresh map and the start of the next one.
        // But for now, this works.....
        let mut address = ADDR_DEFAULT;
        let flags: c_int = libc::MAP_SHARED | libc::MAP_FIXED;
        let mut ptr = libc::MAP_FAILED;

        unsafe {
            for _ in 0..10 {
                debug!("libc::mmap: a: {:#x}, s: {}, p: {}, f: {}, fd:, {}",
                    address, size, prot, flags, fd
                );

                ptr = address as _;
                ptr = libc::mmap(ptr, size as _, prot, flags, fd, 0);
                let rc = ptr as usize;

                debug!("got {:#x}", rc);
                if rc == address {
                    break;
                }

                address += ADDR_DEFAULT_INCR;
            }
        }

        if ptr == libc::MAP_FAILED {
            return Err(Error::BadMmap {
                source: io::Error::last_os_error(),
            });
        }

        Ok(address)
    }

    fn do_headers(addr: usize, info: &mut MmapInfo)
                  -> Vec<Offset> {
        let mut vec = Vec::new();
        let mut off = align_offset(MMAP_INFO_SIZE);

        for i in 0 .. info.num_headers {
            vec.push(Offset(off));
            let h: &MmapHeader = unsafe {
                mem::transmute::<_,_>(addr + off as usize)
            };

            debug!("headers: {} {:#x}: '{:#?}', kind: {}, 
                      flags: {:#x}, size: {}",
                     i, off, h.name, h.kind, h.flags, h.size);

            off += align_offset(h.size);
        }

        vec
    }

    pub fn open<'a>(self) -> Result<Box<Mmap<'a>>, Error> {
        let mut size = self.size;
        let style;
        let fd;
        let mut prot: c_int = libc::PROT_READ;
        let created;

        // Based on whether the caller set a path, we build a 'style'
        // object for File or Anonymous.  We also set 'fd' and
        // 'created', based on the style.
        if let Some(path) = self.path {
            let write = self.write;

            // We can set the write bits in the file permissions
            // knowning that it will only matter if the file is
            // created, which only happens if 'write' is true.
            let perm = if self.private { 0o600 } else { 0o644 };

            // XXX Can't find a means of seeing if .open created a
            // file, so we check before hand, which opens a Window
            // Of Uncertainty, but .....
            created = ! fs::metadata(&path).is_ok();

            let file = OpenOptions::new()
                .read(true)
                .write(write)
                .create(write)
                .mode(perm)
                .open(&path)
                .context(BadFile { path: &path })?;

            if self.write {
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
            prot |= libc::PROT_WRITE;

            let segments = Vec::new();
            style = MmapStyle::Anonymous { segments };
        }

        let addr = MmapBuilder::do_map(fd, size, prot)?;
        let address = Address(addr);

        debug!("address {:#x} {}", addr,
               if created { "created" } else { "exists" });

        // We cast the address into an "info" structure, which is the
        // first header in the memory mapped space.
        let info: &mut MmapInfo
            = unsafe { mem::transmute::<_, _>(addr) };

        info.validate(addr, size, created)?;
        let headers = MmapBuilder::do_headers(addr, info);

        let b = Box::new(Mmap { address, size, style, info, headers});
        Ok(b)
    }
}

// Return the number of atoms to needed for a given size of allocation
fn num_atoms_needed(size: usize, atom_shift: u32) -> AtomCount {
    let m1: usize = (1 << atom_shift) - 1;
    let count: usize = (size + m1) >> atom_shift;
    AtomCount(count as u32)
}

fn atom_to_addr(addr: Address, atom: RawAtom, shift: u32) -> usize {
    let uatom = atom as usize;
    addr.0 + (uatom << shift)
}

impl Mmap<'_> {
    pub fn alloc(&mut self, size: usize) -> Option<Atom> {
        if size == 0 {
            warn!("Mmap::alloc called with zero size");
            return None;
        }

        let count: AtomCount = num_atoms_needed(size, ATOM_SHIFT);

        let info = &mut self.info;
        let mut lastp: &mut FreeAtom = &mut info.free;

        debug!("mmap::alloc: need {:?}", count);
        loop {
            let mut fa = lastp.0;
            if fa == ATOM_NULL {
                break;
            }

            let addr = atom_to_addr(self.address, fa, ATOM_SHIFT);

            let freep: &mut MmapFree
                = unsafe { mem::transmute::<_, _>(addr) };

            assert_eq!(freep.magic, FREE_MAGIC);

            if freep.size < count {
                // Not enough space; skip
                lastp = &mut freep.next;
                continue;

            } else if freep.size == count { // Perfect match
                *lastp = freep.next;      // Remove this node

            } else {
                freep.size -= count; // We take the "top" atoms off this node
                fa += freep.size.0;
            }
            
            return Some(Atom(fa));
        }

        let new_count = AtomCount({
            if count.0 < DEFAULT_COUNT { DEFAULT_COUNT }
            else { round_up_atom(count.0, DEFAULT_COUNT) }
        });

        let old_size = info.size;
        let new_size = old_size + (new_count.0 as usize) << ATOM_SHIFT;

        if info.max_size != 0 && new_size > info.max_size {
            warn!("mmap::alloc: max size reached {}", info.max_size);
            return None;
        }

        match &self.style {
            MmapStyle::Unset => (),
            MmapStyle::File { ref file, fd, write, .. } => {

            },
            MmapStyle::Anonymous { ref segments } => {

            },
        }

        None
    }

    #[allow(dead_code)]
    fn dump(&mut self) {
        debug!("mmap::dump_free: {:p}", &self);

        let mut lastp: &mut FreeAtom = &mut self.info.free;

        debug!("  free list");
        loop {
            let fa = lastp.0;
            if fa == ATOM_NULL {
                break;
            }

            let addr = atom_to_addr(self.address, fa, ATOM_SHIFT);

            let freep: &mut MmapFree
                = unsafe { mem::transmute::<_, _>(addr) };

            debug!("    {:#x} {:p} size {:?}, next {:?}",
                     addr, freep, freep.size, freep.next);

            assert_eq!(freep.magic, FREE_MAGIC);

            lastp = &mut freep.next;
        }
    }
}

impl AddAssign for AtomCount {
    fn add_assign(&mut self, other: Self) {
        self.0 += other.0;
    }
}

impl SubAssign for AtomCount {
    fn sub_assign(&mut self, other: Self) {
        self.0 -= other.0;
    }
}

impl SubAssign<u32> for AtomCount {
    fn sub_assign(&mut self, other: u32) {
        self.0 -= other;
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
            .field("free", &self.free.0)
            .finish()
    }
}

impl Debug for Mmap<'_> {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        let Address(addr) = self.address;
        f.debug_struct("Mmap")
            .field("address", &format_args!("0x{:#x}", addr))
            .field("size", &self.size)
            .field("style", &self.style)
            .field("style", &self.info)
            .finish()
    }
}

#[cfg(test)]
fn init() {
    let _ = env_logger::builder().is_test(true).try_init();
}

#[test]
fn simple_open() {
    init();

    error!("some failure");

    let rc = MmapBuilder::new()
        .path(PathBuf::from("test.mem"))
        .write(true)
        .size(100 * 1024)
        .open();

    debug!("got {:?}", rc);
    if let Ok(mut m) = rc {
        m.dump();
        let mine = m.alloc(3000);
        debug!("alloc {:?}", mine);
        let mine = m.alloc(6000);
        debug!("alloc {:?}", mine);
        let mine = m.alloc(9000);
        debug!("alloc {:?}", mine);
        m.dump();
    }
}

