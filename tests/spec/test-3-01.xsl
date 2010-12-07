<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:axsl="http://www.w3.org/1999/XSL/TransformAlias" version="1.0">
  <xsl:namespace-alias stylesheet-prefix="axsl" result-prefix="xsl"/>
  <xsl:output method="html" version="4.0"/>
  <xsl:decimal-format name="name" decimal-separator="char" grouping-separator="char" infinity="string" minus-sign="char" NaN="string" percent="char" per-mille="char" zero-digit="char" digit="char" pattern-separator="char"/>
  <xsl:output method="html" version="nmtoken" encoding="string" omit-xml-declaration="yes" standalone="yes" doctype-public="string" doctype-system="string" cdata-section-elements="qname1 qname2 qname3" indent="yes" media-type="string">
    <!-- or "xml" | "html" | "text" | qname-but-not-ncname -->
    <!-- or "no" -->
    <!-- or "no" -->
    <!-- Or  "no" -->
  </xsl:output>
  <xsl:variable name="two" select="&quot;2&quot;"/>
  <xsl:key name="my-key" match="div" use="@id"/>
  <xsl:attribute-set name="one"/>
  <xsl:attribute-set name="two">
    <xsl:attribute name="{thing}">
      <xsl:value-of select="one"/>
    </xsl:attribute>
  </xsl:attribute-set>
  <xsl:attribute-set name="foo" use-attribute-sets="one two">
    <xsl:attribute name="test{a}">
      <xsl:value-of select="concat(&quot;one&quot;, thing)"/>
    </xsl:attribute>
    <xsl:attribute name="test{b}">
      <xsl:value-of select="concat(&quot;another&quot;, thing)"/>
    </xsl:attribute>
  </xsl:attribute-set>
  <xsl:template match="doc">
    <xsl:variable name="site" select="&quot;test.net&quot;"/>
    <doc>
      <mumble>
        <two>
          <xsl:number value="$two" format="001" lang="en" letter-value="alphabetic" grouping-separator="/" grouping-size="{100}"/>
        </two>
        <!-- 
	     * The following substatements of 'number' can only appear if
	     * there is no argument to the number statement.
 -->
        <three>
          <xsl:number level="any" from="h1" count="h2 | h3 | h4">
            <!-- or 'single' or 'any' -->
          </xsl:number>
        </three>
      </mumble>
      <xsl:copy use-attribute-sets="foo">
        <xsl:apply-templates select="@* | node()"/>
      </xsl:copy>
      <xsl:for-each select="*">
        <xsl:sort select="size" data-type="text" lang="en" order="ascending" case-order="upper-first"/>
        <!-- or "number" or qname-but-not-ncname -->
        <!-- or "descending" -->
        <!-- or "lower-first" -->
        <xsl:copy/>
      </xsl:for-each>
      <xsl:message>before</xsl:message>
      <xsl:message terminate="yes">
        <xsl:value-of select="concat(one, &quot; is  good&quot;)"/>
      </xsl:message>
      <xsl:message terminate="yes">too late</xsl:message>
      <xsl:message>after</xsl:message>
    </doc>
    <xsl:some-fancy-element>
      <xsl:fallback>
        <xsl:message>not implemented</xsl:message>
      </xsl:fallback>
    </xsl:some-fancy-element>
  </xsl:template>
</xsl:stylesheet>
