<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:slax="http://xml.libslax.org/slax" xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" version="1.0" extension-element-prefixes="slax slax-ext">
  <xsl:output xmlns:slax="http://xml.libslax.org/slax" method="xml" slax:make="json" indent="yes"/>
  <xsl:variable name="test-temp-1">
    <one>
      <xml>
        <my-top>
          <a type="number">1</a>
          <b type="number">2.5e-5</b>
        </my-top>
      </xml>
    </one>
    <two>
      <xml>
        <my-top type="array">
          <member type="number">1</member>
          <member type="number">2</member>
          <member type="number">3</member>
          <member type="number">4</member>
        </my-top>
      </xml>
    </two>
    <three>
      <xml>
        <my-top>
          <a type="number">1</a>
          <b type="array">
            <member type="number">2</member>
            <member type="number">3</member>
          </b>
        </my-top>
      </xml>
    </three>
    <four>
      <xml>
        <my-top type="array">
          <member type="member">
            <a type="number">1</a>
          </member>
          <member type="member">
            <b type="number">2</b>
          </member>
          <member type="member">
            <c type="number">3</c>
          </member>
        </my-top>
      </xml>
    </four>
    <five>
      <xml>
        <my-top>
          <a>fish</a>
          <b>tur key</b>
          <c>mon key suit</c>
        </my-top>
      </xml>
    </five>
    <six>
      <xml>
        <my-top>
          <Image>
            <Width type="number">800</Width>
            <Height type="number">600</Height>
            <Title>View from 15th Floor</Title>
            <Thumbnail>
              <Url>http://www.example.com/image/481989943</Url>
              <Height type="number">125</Height>
              <Width>100</Width>
            </Thumbnail>
            <IDs type="array">
              <member type="number">116</member>
              <member type="number">943</member>
              <member type="number">234</member>
              <member type="number">38793</member>
            </IDs>
          </Image>
        </my-top>
      </xml>
    </six>
    <seven>
      <xml>
        <my-top type="array">
          <member type="member">
            <precision>zip</precision>
            <Latitude type="number">37.7668</Latitude>
            <Longitude type="number">-122.3959</Longitude>
            <Address/>
            <City>SAN FRANCISCO</City>
            <State>CA</State>
            <Zip>94107</Zip>
            <Country>US</Country>
          </member>
          <member type="member">
            <precision>zip</precision>
            <Latitude type="number">37.371991</Latitude>
            <Longitude type="number">-122.026020</Longitude>
            <Address/>
            <City>SUNNYVALE</City>
            <State>CA</State>
            <Zip>94085</Zip>
            <Country>US</Country>
          </member>
        </my-top>
      </xml>
    </seven>
    <eight>
      <xml>
        <my-top type="array">
          <member type="member">
            <a>nrt
&#13;	nrt</a>
          </member>
        </my-top>
      </xml>
    </eight>
    <nine>
      <xml>
        <my-top>
          <name>Skip Tracer</name>
          <location>The city that never sleeps</location>
          <age type="number">5</age>
          <real type="false">false</real>
          <cases type="null">null</cases>
          <equipment type="array">
            <member type="member">hat</member>
            <member type="member">desk</member>
            <member type="member">attitude</member>
          </equipment>
        </my-top>
      </xml>
    </nine>
    <ten>
      <xml>
        <my-top>
          <element name="the&#9;end" type="number">1</element>
          <element name="moment of truth" type="number">2.5e-5</element>
          <element name="3com">dead</element>
        </my-top>
      </xml>
    </ten>
  </xsl:variable>
  <xsl:variable xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="test" select="slax-ext:node-set($test-temp-1)"/>
  <xsl:template match="/">
    <results>
      <xsl:copy-of select="$test"/>
    </results>
  </xsl:template>
</xsl:stylesheet>
