use std::{
    cell::UnsafeCell,
    fmt,
    ops::{Deref, DerefMut},
    sync::atomic::{AtomicBool, Ordering::*},
};

#[derive(Debug)]
pub struct Mutex<T> {
    locked: AtomicBool,
    inner: UnsafeCell<T>,
}

#[derive(Debug, Clone, Copy)]
pub struct MutexErr;

pub struct MutexGuard<'a, T>
where
    T: 'a,
{
    mutex: &'a Mutex<T>,
}

impl<T> Mutex<T> {
    pub fn new(data: T) -> Self {
        Self {
            locked: AtomicBool::new(false),
            inner: UnsafeCell::new(data),
        }
    }

    pub fn try_lock<'a>(&'a self) -> Result<MutexGuard<'a, T>, MutexErr> {
        if self.locked.swap(true, Acquire) {
            Err(MutexErr)
        } else {
            Ok(MutexGuard { mutex: self })
        }
    }

    pub fn lock<'a>(&'a self) -> MutexGuard<'a, T> {
        loop {
            if let Ok(m) = self.try_lock() {
                break m;
            }
        }
    }
}

unsafe impl<T> Send for Mutex<T> where T: Send {}

unsafe impl<T> Sync for Mutex<T> where T: Send {}

impl<T> Drop for Mutex<T> {
    fn drop(&mut self) {
        unsafe { self.inner.get().drop_in_place() }
    }
}

impl<'a, T> Deref for MutexGuard<'a, T> {
    type Target = T;

    fn deref(&self) -> &T {
        unsafe { &*self.mutex.inner.get() }
    }
}

impl<'a, T> DerefMut for MutexGuard<'a, T> {
    fn deref_mut(&mut self) -> &mut T {
        unsafe { &mut *self.mutex.inner.get() }
    }
}

impl<'a, T> fmt::Debug for MutexGuard<'a, T>
where
    T: fmt::Debug,
{
    fn fmt(&self, fmtr: &mut fmt::Formatter) -> fmt::Result {
        write!(fmtr, "{:?}", &**self)
    }
}

impl<'a, T> Drop for MutexGuard<'a, T> {
    fn drop(&mut self) {
        let _prev = self.mutex.locked.swap(false, Release);
        debug_assert!(_prev);
    }
}
