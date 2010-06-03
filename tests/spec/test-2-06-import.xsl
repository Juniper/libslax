<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet
                xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns:one="http://one.example.org"
                xmlns:two="http://two.example.org"
                xmlns:three="http://three.example.org"
                version="1.0">

  <xsl:template match="/doc2">
    <one:root xmlns="http://six.example.com">
      <element1 xmlns="http://four.example.com">
        <element2>Content 2</element2>
      </element1>
      <element3 xmlns="http://five.example.com">
        <element4>Content 4</element4>
        <two:element6>Content 6</two:element6>
      </element3>
      <element5>Content 5</element5>
    </one:root>
  </xsl:template>
</xsl:stylesheet>
