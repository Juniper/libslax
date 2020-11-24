use std::fs;
use std::path::Path;

use toml;
use toml::Value::{Array, Table};

pub struct Config {
    root: toml::Value, // Top of the config tree
}

impl Config {
    pub fn from(path: &Path) -> Result<Config, std::io::Error> {
        let data = fs::read_to_string(path)?;
        let root = toml::from_str(&data)?;

        Ok(Config { root })
    }

    pub fn from_str(data: &str) -> Result<Config, std::io::Error> {
        let root = toml::from_str(&data)?;

        Ok(Config { root })
    }

    pub fn get(&self, ident: &str) -> Option<&toml::Value> {
        let names = ident.split(".");
        self.get_from_iter(names.into_iter())
    }

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
                _ => (),
            }

            return None;
        }

        Some(node) // The current node is the answer
    }

    pub fn get_integer(&self, ident: &str) -> Option<i64> {
        match self.get(ident) {
            Some(val) => val.as_integer(),
            _ => None,
        }
    }

    pub fn get_str(&self, ident: &str) -> Option<&str> {
        match self.get(ident) {
            Some(val) => val.as_str(),
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
"##,
    )
    .unwrap();

    assert_eq!(Some("one"), config.get("main.first").unwrap().as_str());
    assert_eq!(Some("two"), config.get("main.second").unwrap().as_str());
    assert_eq!(Some(3), config.get("main.third").unwrap().as_integer());

    assert_eq!(Some("one"), config.get_str("main.first"));
    assert_eq!(Some("two"), config.get_str("main.second"));
    assert_eq!(Some(3), config.get_integer("main.third"));

    assert_eq!("one", config.get_str("main.first").unwrap_or("one"));
    assert_eq!("four", config.get_str("main.fourth").unwrap_or("four"));
    assert_eq!(5, config.get_integer("main.fifth").unwrap_or(5));

    assert_eq!(Some("sure"), config.get_str("main.sub1.sub2.sub3.sub4"));

    let info = config.get("some.info.set").unwrap();
    assert_eq!(Some(1), info[0].as_integer());
    assert_eq!(Some(2), info[1].as_integer());
    assert_eq!(Some(3), info[2].as_integer());

    let v = vec!["main", "third"];
    assert_eq!(Some(3), config.get_from_iter(v.into_iter())
                              .unwrap().as_integer());
}
