use sxd_document::parser;
use sxd_xpath::{Factory, Context};

use snafu::{Snafu, ResultExt};

use leghorn::spinlock;

#[derive(Debug, Snafu)]
enum Error {
    #[snafu(display("Parse failed for input string: '{}'", content))]
    ParseFailed { content: String, source: sxd_document::parser::Error },
    #[snafu(display("Could not open config: {}", "source"))]
    SaveConfig { source: std::io::Error },
    #[snafu(display("The user id {} is invalid", "user_id"))]
    UserIdInvalid { user_id: i32 },
}

type Result<T, E = Error> = std::result::Result<T, E>;

type Base = u32;
struct One { v: Base }
struct Two { v: Base }

#[derive(Debug)]
struct Foo;

pub fn testing() {
    let lock = spinlock::Mutex::<u32>::new(0);
    let mut x = lock.lock();
    *x = 42;
}

fn main() {
    let one = One { v: 54 };
    let two = Two { v: 49 };
    if one.v == two.v {
        println!("yes");
    }
    let three = Foo;
    println!("{:?}", three);
    testing();
    
    let res = test("<rofot>hello</root>");
    match res {
        Ok(()) => (),
        Err(e) => println!("error: {}", e),
    }
}

fn test(input: &str) -> Result<()> {
    println!("starting3");

    let package = parser::parse(input).context(ParseFailed { content: input.to_string() })?;
    let document = package.as_document();

    let factory = Factory::new();
    let xpath = factory.build("/root").expect("Could not compile XPath").unwrap();

    let context = Context::new();

    let value = xpath.evaluate(&context, document.root())
        .expect("XPath evaluation failed");

    println!("result: {}", value.string());
    assert_eq!("hello", value.string());
    Ok(())
}

