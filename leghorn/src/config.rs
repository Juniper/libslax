use std::fs;
use std::path::Path;
use std::str::FromStr;

use toml::Value::{Array, Table};

/// Access fields inside a TOML config file.
///
/// TOML files are hierarchical files with easily understood rules,
/// with the aim of making human-friendly data files.  The standard
/// `toml` crate targets `serdes`-based structure representation, but
/// for our needs, a name-based approach is more useful, where we give
/// the name in a string and get a value of the proper type
/// (e.g. string, integer, etc).   The names are in dotted notation, so
/// "offices.locations.asia" would match:
///
/// ```toml
///    [offices.locations]
///    asia = "Kuala Lumpur"
/// ```
///
/// Arrays are matched via the "name" value:
///
/// ```toml
///    [[cars]]
///    name = "Mustang"
///    make = "Ford"
///    year = 1964
///
///    [[cars]]
///    name = "Stringray"
///    make = "Chevrolet"
///    year = 1968
/// ```
///
/// In this example, "cars.Mustang" would match the first entry.
///
/// NOTES:
/// - This is obviously just a shim around Alex Crichton's "toml" crate.
/// - In the future, I might add some field-based matching, something
///   like "cars.make=Chevrolet.year", so we aren't so tied to the
///   "name" field.
///
/// BUGS:
/// - Currently `config` has no concept of parsing quotes inside names
///   (e.g "films.'Life of Brian'.cast").
///

pub struct Config {
    root: toml::Value, // Top of the config tree
}

impl FromStr for Config {
    type Err = std::io::Error;

    /// Create a new `Config` object from a string.
    fn from_str(data: &str) -> Result<Self, Self::Err> {
        let root = toml::from_str(&data)?;

        Ok(Config { root })
    }
}

impl Config {
    /// Create a new `Config` object from the file given in `path`.
    pub fn from(path: &Path) -> Result<Config, std::io::Error> {
        let data = fs::read_to_string(path)?;
        let root = toml::from_str(&data)?;

        Ok(Config { root })
    }

    /// Retrieve a value from a `Config` dataset.  The name parameter is
    /// the TOML location as a dot-separated list of names, and the returned
    /// value is a raw `toml::Value` object.
    pub fn get(&self, ident: &str) -> Option<&toml::Value> {
        let names = ident.split('.');
        self.get_from_iter(names.into_iter())
    }

    /// Retrieve a value from a `Config` dataset using a `str` iterator
    /// to name the data item.
    pub fn get_from_iter<'a, I>(&self, names: I) -> Option<&toml::Value>
    where
        I: Iterator<Item = &'a str>,
    {
        let mut node = &self.root;

        'name: for name in names {
            match node {
                Table(tab) => {
                    if let Some(next) = tab.get(name) {
                        node = next;
                        continue 'name;
                    }
                }
                Array(arr) => {
                    for elt in arr {
                        if let Some(m) = elt.get("name") {
                            if let Some(s) = m.as_str() {
                                if s == name {
                                    node = elt;
                                    continue 'name;
                                }
                            }
                        }
                    }
                }

                // Anything else means the data does not match the
                // name, so someone is looking at "foo = 15" and
                // trying to get "foo.some.thing.underneath", which
                // can't work.  Arguably, we should return an error,
                // but, well, we don't.
                _ => (),
            }

            return None; // Add the above matches need a "continue"
        }

        Some(node) // The current node is the answer
    }

    /// Return a string value for a `Config` data item
    pub fn get_str(&self, ident: &str) -> Option<&str> {
        match self.get(ident) {
            Some(val) => val.as_str(),
            _ => None,
        }
    }

    /// Return an integer value for a `Config` data item
    pub fn get_i64(&self, ident: &str) -> Option<i64> {
        match self.get(ident) {
            Some(val) => val.as_integer(),
            _ => None,
        }
    }

    /// Return a floating point value for a `Config` data item
    pub fn get_f64(&self, ident: &str) -> Option<f64> {
        match self.get(ident) {
            Some(val) => val.as_float(),
            _ => None,
        }
    }

    /// Return a boolean value for a `Config` data item
    pub fn get_bool(&self, ident: &str) -> Option<bool> {
        match self.get(ident) {
            Some(val) => val.as_bool(),
            _ => None,
        }
    }
}

#[test]
fn test_simple() {
    let config = Config::from_str(
        r##"
[main]
first = "one"
second = "two"
third = 3
sub1.sub2.sub3.sub4 = "sure"

[some.info]
set = [ 1, 2, 3 ]

[data]
pi = 3.1415926535
cool = true
"##,
    )
    .unwrap();

    assert_eq!(Some("one"), config.get("main.first").unwrap().as_str());
    assert_eq!(Some("two"), config.get("main.second").unwrap().as_str());
    assert_eq!(Some(3), config.get("main.third").unwrap().as_integer());

    assert_eq!(Some("one"), config.get_str("main.first"));
    assert_eq!(Some("two"), config.get_str("main.second"));
    assert_eq!(Some(3), config.get_i64("main.third"));

    assert_eq!("one", config.get_str("main.first").unwrap_or("one"));
    assert_eq!("four", config.get_str("main.fourth").unwrap_or("four"));
    assert_eq!(5, config.get_i64("main.fifth").unwrap_or(5));

    assert_eq!(Some("sure"), config.get_str("main.sub1.sub2.sub3.sub4"));

    let info = config.get("some.info.set").unwrap();
    assert_eq!(Some(1), info[0].as_integer());
    assert_eq!(Some(2), info[1].as_integer());
    assert_eq!(Some(3), info[2].as_integer());

    let v = vec!["main", "third"];
    assert_eq!(
        Some(3),
        config.get_from_iter(v.into_iter()).unwrap().as_integer()
    );

    let pi = config.get_f64("data.pi").unwrap_or(0.0);
    assert_eq!(pi, 3.1415926535);

    assert_eq!(Some(true), config.get_bool("data.cool"));
}

#[test]
fn test_docs() {
    let data = r##"
[offices.locations]
asia = "Kuala Lumpur"

[[cars]]
name = "Mustang"
make = "Ford"
year = 1964

[[cars]]
name = "Stringray"
make = "Chevrolet"
year = 1968

"##;

    let config = Config::from_str(data).unwrap();

    assert_eq!(
        Some("Kuala Lumpur"),
        config.get_str("offices.locations.asia")
    );

    assert_eq!(Some(1964), config.get_i64("cars.Mustang.year"));
    assert_eq!(None, config.get_i64("cars.Chevrolet.year"));
    assert_eq!(Some(1968), config.get_i64("cars.Stringray.year"));
}
